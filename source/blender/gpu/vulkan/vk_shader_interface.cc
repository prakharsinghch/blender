/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2023 Blender Foundation. All rights reserved. */

/** \file
 * \ingroup gpu
 */

#include "vk_shader_interface.hh"

namespace blender::gpu {

void VKShaderInterface::init(const shader::ShaderCreateInfo &info)
{
  using namespace blender::gpu::shader;

  attr_len_ = 0;
  uniform_len_ = 0;
  ssbo_len_ = 0;
  ubo_len_ = 0;
  image_offset_ = -1;

  Vector<ShaderCreateInfo::Resource> all_resources;
  all_resources.extend(info.pass_resources_);
  all_resources.extend(info.batch_resources_);

  for (ShaderCreateInfo::Resource &res : all_resources) {
    switch (res.bind_type) {
      case ShaderCreateInfo::Resource::BindType::IMAGE:
        uniform_len_++;
        break;
      case ShaderCreateInfo::Resource::BindType::SAMPLER:
        image_offset_ = max_ii(image_offset_, res.slot);
        uniform_len_++;
        break;
      case ShaderCreateInfo::Resource::BindType::UNIFORM_BUFFER:
        ubo_len_++;
        break;
      case ShaderCreateInfo::Resource::BindType::STORAGE_BUFFER:
        ssbo_len_++;
        break;
    }
  }
  /* Make sure that the image slots don't overlap with the sampler slots.*/
  image_offset_++;

  int32_t input_tot_len = ubo_len_ + uniform_len_ + ssbo_len_;
  inputs_ = static_cast<ShaderInput *>(
      MEM_calloc_arrayN(input_tot_len, sizeof(ShaderInput), __func__));
  ShaderInput *input = inputs_;

  name_buffer_ = (char *)MEM_mallocN(info.interface_names_size_, "name_buffer");
  uint32_t name_buffer_offset = 0;

  /* Uniform blocks */
  for (const ShaderCreateInfo::Resource &res : all_resources) {
    if (res.bind_type == ShaderCreateInfo::Resource::BindType::UNIFORM_BUFFER) {
      copy_input_name(input, res.image.name, name_buffer_, name_buffer_offset);
      input->location = input->binding = res.slot;
      input++;
    }
  }

  /* Images, Samplers and buffers. */
  for (const ShaderCreateInfo::Resource &res : all_resources) {
    if (res.bind_type == ShaderCreateInfo::Resource::BindType::SAMPLER) {
      copy_input_name(input, res.sampler.name, name_buffer_, name_buffer_offset);
      input->location = input->binding = res.slot;
      input++;
    }
    else if (res.bind_type == ShaderCreateInfo::Resource::BindType::IMAGE) {
      copy_input_name(input, res.image.name, name_buffer_, name_buffer_offset);
      input->location = input->binding = res.slot + image_offset_;
      input++;
    }
  }

  /* Storage buffers */
  for (const ShaderCreateInfo::Resource &res : all_resources) {
    if (res.bind_type == ShaderCreateInfo::Resource::BindType::STORAGE_BUFFER) {
      copy_input_name(input, res.storagebuf.name, name_buffer_, name_buffer_offset);
      input->location = input->binding = res.slot;
      input++;
    }
  }

  sort_inputs();

  /* Determine the descriptor set locations after the inputs have been sorted.*/
  descriptor_set_locations_ = Array<VKDescriptorSet::Location>(input_tot_len);
  uint32_t descriptor_set_location = 0;
  for (ShaderCreateInfo::Resource &res : all_resources) {
    const ShaderInput *input = shader_input_get(res);
    descriptor_set_location_update(input, descriptor_set_location++);
  }
}

static int32_t shader_input_index(const ShaderInput *shader_inputs,
                                  const ShaderInput *shader_input)
{
  int32_t index = (shader_input - shader_inputs);
  return index;
}

void VKShaderInterface::descriptor_set_location_update(const ShaderInput *shader_input,
                                                       const VKDescriptorSet::Location location)
{
  int32_t index = shader_input_index(inputs_, shader_input);
  descriptor_set_locations_[index] = location;
}

const VKDescriptorSet::Location VKShaderInterface::descriptor_set_location(
    const ShaderInput *shader_input) const
{
  int32_t index = shader_input_index(inputs_, shader_input);
  return descriptor_set_locations_[index];
}

const VKDescriptorSet::Location VKShaderInterface::descriptor_set_location(
    const shader::ShaderCreateInfo::Resource &resource) const
{
  const ShaderInput *shader_input = shader_input_get(resource);
  BLI_assert(shader_input);
  return descriptor_set_location(shader_input);
}

const VKDescriptorSet::Location VKShaderInterface::descriptor_set_location(
    const shader::ShaderCreateInfo::Resource::BindType &bind_type, int binding) const
{
  const ShaderInput *shader_input = shader_input_get(bind_type, binding);
  BLI_assert(shader_input);
  return descriptor_set_location(shader_input);
}

const ShaderInput *VKShaderInterface::shader_input_get(
    const shader::ShaderCreateInfo::Resource &resource) const
{
  return shader_input_get(resource.bind_type, resource.slot);
}

const ShaderInput *VKShaderInterface::shader_input_get(
    const shader::ShaderCreateInfo::Resource::BindType &bind_type, int binding) const
{
  switch (bind_type) {
    case shader::ShaderCreateInfo::Resource::BindType::IMAGE:
      return texture_get(binding + image_offset_);
    case shader::ShaderCreateInfo::Resource::BindType::SAMPLER:
      return texture_get(binding);
    case shader::ShaderCreateInfo::Resource::BindType::STORAGE_BUFFER:
      return ssbo_get(binding);
    case shader::ShaderCreateInfo::Resource::BindType::UNIFORM_BUFFER:
      return ubo_get(binding);
  }
  return nullptr;
}

}  // namespace blender::gpu
