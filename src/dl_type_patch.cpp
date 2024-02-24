#include "dl_types.h"

#include <dl/dl.h>
#include <inttypes.h>

static dl_error_t dl_patch(dl_ctx_t dl_target_ctx, void* patch_ctx, dl_patch_params* patch_params, dl_error_msg_handler error_f, void* error_ctx)
{
	(void)error_f;
	(void)error_ctx;

	dl_ctx_t dl_source_ctx = (dl_ctx_t)patch_ctx;
	const dl_type_desc* source_type = dl_internal_find_type( dl_source_ctx, patch_params->input_type );
	if( nullptr == source_type )
		return DL_ERROR_TYPE_NOT_FOUND;

	const dl_type_desc* target_type = dl_internal_find_type( dl_target_ctx, patch_params->wanted_type );
	if( nullptr == target_type )
		return DL_ERROR_TYPE_NOT_FOUND;

	const uint8_t* instance_to_patch = patch_params->instance;
	uint8_t* out_instance = patch_params->out_instance;
	if (patch_params->packed_instance_as_input)
		instance_to_patch += dl_internal_align_up( sizeof( dl_data_header ), source_type->alignment[DL_PTR_SIZE_HOST] );

	size_t used_size = 0;
	size_t header_size = dl_internal_align_up( sizeof( dl_data_header ), target_type->alignment[DL_PTR_SIZE_HOST] );
	dl_data_header* out_header = nullptr;
	if (patch_params->packed_instance_as_output)
	{
		used_size += header_size;

		if (patch_params->out_instance_size >= used_size)
		{
			out_header = (dl_data_header*)out_instance;
			out_instance += header_size;
			out_header->id = DL_INSTANCE_ID;
			out_header->version = DL_INSTANCE_VERSION;
			out_header->root_instance_type = patch_params->wanted_type;
			out_header->is_64_bit_ptr = DL_PTR_SIZE_HOST == DL_PTR_SIZE_64BIT;

			// Written at end of function if successful
			out_header->instance_size = 0;
			out_header->not_using_ptr_chain_patching = 0;
			out_header->first_pointer_to_patch = 0;
		}
	}

	used_size += target_type->size[DL_PTR_SIZE_HOST];
	for (uint32_t tgt_i = 0; tgt_i < target_type->member_count; ++tgt_i)
	{
		const dl_member_desc* tgt_member = dl_get_type_member( dl_target_ctx, target_type, tgt_i );
		uint32_t src_i;
		for (src_i = 0; src_i < source_type->member_count; ++src_i)
		{
			const dl_member_desc* src_member = dl_get_type_member( dl_source_ctx, source_type, src_i );
			if (strcmp(dl_internal_member_name(dl_target_ctx, tgt_member), dl_internal_member_name(dl_source_ctx, src_member)) == 0)
			{
				// Source with the same name exists
				if (tgt_member->type == src_member->type && tgt_member->type_id == src_member->type_id)
				{
					// The type is the same. Alignment and offset might have changed but a memcpy should work
					if (patch_params->out_instance_size >= used_size)
						memcpy(patch_params->out_instance + tgt_member->offset[DL_PTR_SIZE_HOST], instance_to_patch + src_member->offset[DL_PTR_SIZE_HOST], tgt_member->size[DL_PTR_SIZE_HOST]);
					break; // Break inner loop
				}
				if (tgt_member->type == src_member->type && tgt_member->AtomType() == DL_TYPE_ATOM_POD && tgt_member->StorageType() == DL_TYPE_STORAGE_STRUCT)
				{
					dl_patch_params inner_patch_params = *patch_params;
					dl_error_t err = dl_instance_patch(dl_target_ctx, &inner_patch_params);
					if (DL_ERROR_OK == err)
						break;
					return err;
				}
				if (tgt_member->AtomType() == DL_TYPE_ATOM_POD && tgt_member->StorageType() >= DL_TYPE_STORAGE_INT8 && tgt_member->StorageType() <= DL_TYPE_STORAGE_STR &&
					src_member->AtomType() == DL_TYPE_ATOM_POD && src_member->StorageType() >= DL_TYPE_STORAGE_INT8 && src_member->StorageType() <= DL_TYPE_STORAGE_STR)
				{
					// These types might be convertible by converting to string and back?
					char conversion_buffer[1024];
					int res;
					switch(src_member->StorageType())
					{
						case DL_TYPE_STORAGE_INT8:
						case DL_TYPE_STORAGE_ENUM_INT8:
							res = snprintf(conversion_buffer, sizeof(conversion_buffer), "%d", *(int8_t*)(instance_to_patch + src_member->offset[DL_PTR_SIZE_HOST]));
							break;
						case DL_TYPE_STORAGE_INT16:
						case DL_TYPE_STORAGE_ENUM_INT16:
							res = snprintf(conversion_buffer, sizeof(conversion_buffer), "%d", *(int16_t*)(instance_to_patch + src_member->offset[DL_PTR_SIZE_HOST]));
							break;
						case DL_TYPE_STORAGE_INT32:
						case DL_TYPE_STORAGE_ENUM_INT32:
							res = snprintf(conversion_buffer, sizeof(conversion_buffer), "%d", *(int32_t*)(instance_to_patch + src_member->offset[DL_PTR_SIZE_HOST]));
							break;
						case DL_TYPE_STORAGE_INT64:
						case DL_TYPE_STORAGE_ENUM_INT64:
							res = snprintf(conversion_buffer, sizeof(conversion_buffer), "%lld", *(int64_t*)(instance_to_patch + src_member->offset[DL_PTR_SIZE_HOST]));
							break;
						case DL_TYPE_STORAGE_UINT8:
						case DL_TYPE_STORAGE_ENUM_UINT8:
							res = snprintf(conversion_buffer, sizeof(conversion_buffer), "%u", *(uint8_t*)(instance_to_patch + src_member->offset[DL_PTR_SIZE_HOST]));
							break;
						case DL_TYPE_STORAGE_UINT16:
						case DL_TYPE_STORAGE_ENUM_UINT16:
							res = snprintf(conversion_buffer, sizeof(conversion_buffer), "%u", *(uint16_t*)(instance_to_patch + src_member->offset[DL_PTR_SIZE_HOST]));
							break;
						case DL_TYPE_STORAGE_UINT32:
						case DL_TYPE_STORAGE_ENUM_UINT32:
							res = snprintf(conversion_buffer, sizeof(conversion_buffer), "%u", *(uint32_t*)(instance_to_patch + src_member->offset[DL_PTR_SIZE_HOST]));
							break;
						case DL_TYPE_STORAGE_UINT64:
						case DL_TYPE_STORAGE_ENUM_UINT64:
							res = snprintf(conversion_buffer, sizeof(conversion_buffer), "%llu", *(uint64_t*)(instance_to_patch + src_member->offset[DL_PTR_SIZE_HOST]));
							break;
						case DL_TYPE_STORAGE_FP32:
							res = snprintf(conversion_buffer, sizeof(conversion_buffer), "%.9g", *(float*)(instance_to_patch + src_member->offset[DL_PTR_SIZE_HOST]));
							break;
						case DL_TYPE_STORAGE_FP64:
							res = snprintf(conversion_buffer, sizeof(conversion_buffer), "%.17lg", *(double*)(instance_to_patch + src_member->offset[DL_PTR_SIZE_HOST]));
							break;
						case DL_TYPE_STORAGE_STR:
							strncpy(conversion_buffer, (const char*)(instance_to_patch + src_member->offset[DL_PTR_SIZE_HOST]), sizeof(conversion_buffer));
							break;
						default:
							DL_ASSERT(false);
					}
					if (DL_TYPE_STORAGE_STR == tgt_member->StorageType())
					{
						DL_ASSERT(false);
						break;
					}
					else
					{
						if (patch_params->out_instance_size >= used_size)
						{
							const char* dl_conversion_table[] = { "%" SCNi8, "%" SCNi16, "%" SCNi32, "%" SCNi64, "%" SCNu8, "%" SCNu16, "%" SCNu32, "%" SCNu64, "%g", "%lg",
																  "%" SCNi8, "%" SCNi16, "%" SCNi32, "%" SCNi64, "%" SCNu8, "%" SCNu16, "%" SCNu32, "%" SCNu64 };
							int scan_res = sscanf(conversion_buffer, dl_conversion_table[tgt_member->StorageType()], (const char*)(patch_params->out_instance + tgt_member->offset[DL_PTR_SIZE_HOST]));
							(void)scan_res;
						}
						break;
					}
				}
				// This function can't handle more complex type conversions yet. Add a specific function to handle the conversion
				// ADD LOGGING that the type was known, the source and dst was found but type change was to complex
				//dl_log_error( dl_target_ctx, "Type patching wanted to transform type '%s' but ", ... )
				return DL_ERROR_TYPE_NOT_FOUND;
			}
		}
		if (src_i == source_type->member_count)
		{
			// No source member found, apply default value
			if (tgt_member->AtomType() == DL_TYPE_ATOM_BITFIELD)
			{
				DL_ASSERT(false);
				return DL_ERROR_TYPE_NOT_FOUND;
			}
			else if (tgt_member->default_value_size)
			{
				if (patch_params->out_instance_size >= used_size)
					memcpy(patch_params->out_instance + tgt_member->offset[DL_PTR_SIZE_HOST], dl_target_ctx->default_data + tgt_member->default_value_offset, tgt_member->size[DL_PTR_SIZE_HOST]);
				if (tgt_member->default_value_size > tgt_member->size[DL_PTR_SIZE_HOST])
					DL_ASSERT(false); // No support for default values with pointers yet
			}
			else
			{
				dl_log_error( dl_target_ctx, "New member '%s' added without default value, can't patch content", dl_internal_member_name( dl_target_ctx, tgt_member ) );
				return DL_ERROR_TYPE_NOT_FOUND;
			}
		}
	}

	if (patch_params->used_out_instance_size)
		*patch_params->used_out_instance_size = used_size;

	return DL_ERROR_OK;
}

void dl_context_get_patch_func(dl_ctx_t dl_ctx, dl_patch_func* patch_func, void** patch_ctx)
{
	*patch_func = dl_patch;
	*patch_ctx = dl_ctx;
}

dl_error_t dl_instance_patch(dl_ctx_t dl_ctx, dl_patch_params_t* patch_params)
{
	if( patch_params->packed_instance_as_input )
	{
		dl_data_header* header = (dl_data_header*)patch_params->instance;

		if( header->id != DL_INSTANCE_ID_SWAPED && header->id != DL_INSTANCE_ID )
			return DL_ERROR_MALFORMED_DATA;
		if( header->version != DL_INSTANCE_VERSION && header->version != DL_INSTANCE_VERSION_SWAPED )
			return DL_ERROR_VERSION_MISMATCH;

		patch_params->input_type = header->root_instance_type;
	}
	for( uint32_t i = 0; i < patch_params->patch_func_count; ++i )
	{
		dl_error_t err = patch_params->patch_funcs[i]( dl_ctx, patch_params->patch_ctxs[i], patch_params, dl_ctx->error_msg_func, dl_ctx->error_msg_ctx );
		if( DL_ERROR_OK == err )
			return DL_ERROR_OK;
	}
	return DL_ERROR_TYPE_NOT_FOUND;
}
