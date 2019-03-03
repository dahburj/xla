#include "torch_xla/csrc/view.h"

#include <algorithm>
#include <functional>
#include <numeric>

#include "tensorflow/compiler/xla/xla_client/debug_macros.h"
#include "tensorflow/compiler/xla/xla_client/util.h"
#include "torch_xla/csrc/ops/generic_slice.h"
#include "torch_xla/csrc/ops/update_slice.h"
#include "torch_xla/csrc/ops/view.h"

namespace torch_xla {
namespace {

bool IsNarrow(const ViewInfo& view_info) {
  return xla::util::Multiply<xla::int64>(view_info.sizes) !=
         xla::util::Multiply<xla::int64>(view_info.shape.dimensions());
}

ir::Value ApplyViewInfo(ir::Value ir_value, const ViewInfo& view_info) {
  if (IsNarrow(view_info)) {
    return ir::MakeNode<ir::ops::GenericSlice>(ir_value, view_info.indices,
                                               view_info.shape.dimensions());
  } else {
    return ir::MakeNode<ir::ops::View>(ir_value, view_info.shape.dimensions());
  }
}

ir::Value ApplyUpdate(ir::Value ir_value,
                      const Alias::UpdateData& update_data) {
  // We first bring the source IR value forward, by reshaping and slicing.
  std::vector<ir::Value> tmp_values({ir_value});
  for (size_t i = 0; i < update_data.view_infos.size(); ++i) {
    const ViewInfo& view_info = update_data.view_infos[i];
    tmp_values.push_back(ApplyViewInfo(tmp_values.back(), view_info));
  }
  // We then move backward given the source update value, by reshaping and
  // slice-updating.
  ir::Value result = update_data.ir_value;
  for (size_t i = update_data.view_infos.size(); i > 0; --i) {
    const ViewInfo& view_info = update_data.view_infos[i - 1];
    if (IsNarrow(view_info)) {
      result = ir::MakeNode<ir::ops::UpdateSlice>(tmp_values[i - 1], result,
                                                  view_info.indices);
    } else {
      result = ir::MakeNode<ir::ops::View>(result, view_info.sizes);
    }
  }
  return result;
}

}  // namespace

void Alias::Update(ir::Value ir_value, std::vector<ViewInfo> view_infos) {
  updates_.push_back({std::move(ir_value), std::move(view_infos)});
}

ir::Value Alias::CreateUpdateChain() const {
  ir::Value result = ir_value_;
  for (auto& update_data : updates_) {
    result = ApplyUpdate(result, update_data);
  }
  return result;
}

View::View(xla::Shape shape, std::shared_ptr<Alias> alias, ViewInfo view_info)
    : shape_(std::move(shape)), alias_(std::move(alias)) {
  view_infos_.push_back(std::move(view_info));
}

View::View(xla::Shape shape, std::shared_ptr<Alias> alias,
           std::vector<ViewInfo> view_infos)
    : view_infos_(std::move(view_infos)),
      shape_(std::move(shape)),
      alias_(std::move(alias)) {}

void View::Update(ir::Value ir_value) {
  alias_->Update(std::move(ir_value), view_infos_);
}

std::shared_ptr<View> View::CreateSubView(xla::Shape shape,
                                          ViewInfo view_info) {
  std::vector<ViewInfo> view_infos(view_infos_);
  view_infos.push_back(std::move(view_info));
  return std::make_shared<View>(std::move(shape), alias_,
                                std::move(view_infos));
}

View::IrNode View::GetViewIrNode() {
  if (IsUpToDate()) {
    return {ir_value_, false};
  }
  ir::Value update = alias_->CreateUpdateChain();
  for (auto& view_info : view_infos_) {
    update = ApplyViewInfo(update, view_info);
  }
  ir_value_ = update;
  updates_mark_ = alias_->updates().size();
  return {ir_value_, true};
}

}  // namespace torch_xla
