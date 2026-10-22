// The SDK's immediate drawer for its detached overlay mode, over the native
// renderer: Dear ImGui's draw lists are recorded on the UI thread into a frame
// that the renderer's present draws (gpu/overlay.h).
#pragma once

#include <cstring>
#include <memory>

#include <rex/ui/immediate_drawer.h>

#include "gpu/overlay.h"

class OverlayDrawer final : public rex::ui::ImmediateDrawer {
 public:
  class OverlayTexture final : public rex::ui::ImmediateTexture {
   public:
    OverlayTexture(std::shared_ptr<const gpu::overlay::Texture> data)
        : ImmediateTexture(data->width, data->height), data(std::move(data)) {}
    std::shared_ptr<const gpu::overlay::Texture> data;
  };

  OverlayDrawer() = default;

  std::unique_ptr<rex::ui::ImmediateTexture> CreateTexture(uint32_t width, uint32_t height,
                                                           rex::ui::ImmediateTextureFilter filter,
                                                           bool is_repeated, const uint8_t* data) override {
    (void)is_repeated;
    auto texture = std::make_shared<gpu::overlay::Texture>();
    texture->width = width;
    texture->height = height;
    texture->linear = filter == rex::ui::ImmediateTextureFilter::kLinear;
    texture->rgba.resize(size_t(width) * height * 4);
    if (data) std::memcpy(texture->rgba.data(), data, texture->rgba.size());
    return std::make_unique<OverlayTexture>(std::move(texture));
  }

  void Begin(rex::ui::UIDrawContext& context, float width, float height) override {
    ImmediateDrawer::Begin(context, width, height);
    frame_ = std::make_shared<gpu::overlay::Frame>();
    frame_->width = coordinate_space_width();
    frame_->height = coordinate_space_height();
  }

  void BeginDrawBatch(const rex::ui::ImmediateDrawBatch& batch) override {
    base_vertex_ = uint32_t(frame_->vertices.size());
    base_index_ = uint32_t(frame_->indices.size());
    for (int i = 0; i < batch.vertex_count; ++i) {
      const auto& v = batch.vertices[i];
      frame_->vertices.push_back({v.x, v.y, v.u, v.v, v.color});
    }
    if (batch.indices) {
      for (int i = 0; i < batch.index_count; ++i) frame_->indices.push_back(base_vertex_ + batch.indices[i]);
    } else {
      for (int i = 0; i < batch.vertex_count; ++i) frame_->indices.push_back(base_vertex_ + uint32_t(i));
    }
  }

  void Draw(const rex::ui::ImmediateDraw& draw) override {
    if (draw.primitive_type != rex::ui::ImmediatePrimitiveType::kTriangles || draw.count <= 0) return;
    gpu::overlay::Command command;
    if (draw.texture) command.texture = static_cast<OverlayTexture*>(draw.texture)->data;
    command.first_index = base_index_ + uint32_t(draw.index_offset);
    command.index_count = uint32_t(draw.count);
    command.scissor = draw.scissor;
    command.left = draw.scissor_left;
    command.top = draw.scissor_top;
    command.right = draw.scissor_right;
    command.bottom = draw.scissor_bottom;
    frame_->commands.push_back(std::move(command));
  }

  void EndDrawBatch() override {}

  void End() override {
    gpu::overlay::Publish(std::move(frame_));
    frame_.reset();
    recorded_ = true;
    ImmediateDrawer::End();
  }

  // Whether End ran since the last call (the drawer skips frames without dialogs).
  bool TakeRecorded() {
    const bool recorded = recorded_;
    recorded_ = false;
    return recorded;
  }

 private:
  std::shared_ptr<gpu::overlay::Frame> frame_;
  uint32_t base_vertex_ = 0, base_index_ = 0;
  bool recorded_ = false;
};
