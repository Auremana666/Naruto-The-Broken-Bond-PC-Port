// Included after Backend::State, inside gpu::vulkan.
bool Backend::State::ResolvePass(Surface& source, Texture& dest, int32_t sample, std::string& evidence) {
  VkResult result = VK_SUCCESS;
  auto fail = [&](const char* reason) { return GraphicsError(evidence, reason, result); };
  if (!resolve_layout) {
    VkDescriptorSetLayoutBinding binding{0, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
    VkDescriptorSetLayoutCreateInfo set{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    set.bindingCount = 1; set.pBindings = &binding;
    if ((result = vkCreateDescriptorSetLayout(vulkan->vk, &set, nullptr, &resolve_set)) != VK_SUCCESS)
      return fail("native-vulkan-resolve-set-layout");
    VkPushConstantRange push{VK_SHADER_STAGE_FRAGMENT_BIT, 0, 8};
    VkPipelineLayoutCreateInfo layout{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    layout.setLayoutCount = 1; layout.pSetLayouts = &resolve_set;
    layout.pushConstantRangeCount = 1; layout.pPushConstantRanges = &push;
    if ((result = vkCreatePipelineLayout(vulkan->vk, &layout, nullptr, &resolve_layout)) != VK_SUCCESS)
      return fail("native-vulkan-resolve-layout");
  }
  const bool depth = source.desc.format == SurfaceFormat::DepthStencil;
  const char* name = depth ? source.desc.samples > 1 ? "depth_resolve_ms_ps" : "depth_resolve_single_ps" : "sample_resolve_ps";
  auto& pipeline = resolve_pipelines[name];
  if (!pipeline) {
    PipelineDesc desc;
    desc.vs = PassShader("depth_resolve_vs", ShaderStage::kVertex);
    desc.ps = PassShader(name, ShaderStage::kPixel);
    desc.state[0] = 0x00010001; desc.state[1] = 15; desc.state[8] = 1; desc.state[18] = 1;
    const auto made = MakeGraphicsPipeline(vulkan->vk, resolve_layout, desc, {}, pipeline,
        depth ? VK_FORMAT_R32_SFLOAT : VK_FORMAT_R8G8B8A8_UNORM, true);
    if (!made.ok) return GraphicsError(evidence, made.reason, VkResult(made.code));
  }
  auto resources = std::make_shared<GraphicsResources>(); resources->device = vulkan->vk;
  batch.retained.push_back(resources); batch.retained.push_back(pipeline);
  VkImageView input{}, output{};
  if ((result = resources->View(source.Image(), depth ? VK_FORMAT_D32_SFLOAT_S8_UINT : VK_FORMAT_R8G8B8A8_UNORM,
       depth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT, 1, false, kIdentityMapping, input)) != VK_SUCCESS ||
      (result = resources->View(dest.Image(), depth ? VK_FORMAT_R32_SFLOAT : VK_FORMAT_R8G8B8A8_UNORM,
       VK_IMAGE_ASPECT_COLOR_BIT, 1, false, kIdentityMapping, output)) != VK_SUCCESS) return fail("native-vulkan-resolve-view");
  auto* p = static_cast<GraphicsPipeline*>(pipeline.get());
  if ((result = resources->Framebuffer(p->pass, {output}, dest.width, dest.height)) != VK_SUCCESS)
    return fail("native-vulkan-resolve-framebuffer");
  VkDescriptorPoolSize size{VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1};
  VkDescriptorPoolCreateInfo pool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
  pool.maxSets = 1; pool.poolSizeCount = 1; pool.pPoolSizes = &size;
  if ((result = vkCreateDescriptorPool(vulkan->vk, &pool, nullptr, &resources->pool)) != VK_SUCCESS)
    return fail("native-vulkan-resolve-pool");
  VkDescriptorSet set{};
  VkDescriptorSetAllocateInfo alloc{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
  alloc.descriptorPool = resources->pool; alloc.descriptorSetCount = 1; alloc.pSetLayouts = &resolve_set;
  if ((result = vkAllocateDescriptorSets(vulkan->vk, &alloc, &set)) != VK_SUCCESS) return fail("native-vulkan-resolve-set");
  VkDescriptorImageInfo image{VK_NULL_HANDLE, input, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
  VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
  write.dstSet = set; write.descriptorCount = 1; write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE; write.pImageInfo = &image;
  vkUpdateDescriptorSets(vulkan->vk, 1, &write, 0, nullptr);
  batch.list->setFramebuffer(nullptr);
  const auto commands = Commands();
  Transition(commands, source.Image(), source.Aspect(), source.state, kShaderRead);
  Transition(commands, dest.Image(), VK_IMAGE_ASPECT_COLOR_BIT, dest.state, kColorTarget, !dest.content_defined);
  VkRenderPassBeginInfo begin{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
  begin.renderPass = p->pass; begin.framebuffer = resources->framebuffer; begin.renderArea.extent = {dest.width, dest.height};
  vkCmdBeginRenderPass(commands, &begin, VK_SUBPASS_CONTENTS_INLINE);
  VkViewport viewport{0, 0, float(dest.width), float(dest.height), 0, 1};
  VkRect2D scissor{{0, 0}, {dest.width, dest.height}};
  vkCmdSetViewport(commands, 0, 1, &viewport); vkCmdSetScissor(commands, 0, 1, &scissor);
  vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, p->pipeline);
  vkCmdBindDescriptorSets(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, resolve_layout, 0, 1, &set, 0, nullptr);
  const uint32_t constants[]{uint32_t(sample), source.desc.samples};
  vkCmdPushConstants(commands, resolve_layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, 8, constants);
  vkCmdDraw(commands, 3, 1, 0, 0);
  vkCmdEndRenderPass(commands);
  return true;
}
