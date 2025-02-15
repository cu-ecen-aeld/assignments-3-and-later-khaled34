/**
 * @file aesd-circular-buffer.c
 * @brief Functions and data related to a circular buffer imlementation
 *
 * @author Dan Walkes
 * @date 2020-03-01
 * @copyright Copyright (c) 2020
 *
 */

#ifdef __KERNEL__
#include <linux/string.h>
#else
#include <string.h>
#endif

#include "aesd-circular-buffer.h"

/**
 * @param buffer the buffer to search for corresponding offset.  Any necessary locking must be performed by caller.
 * @param char_offset the position to search for in the buffer list, describing the zero referenced
 *      character index if all buffer strings were concatenated end to end
 * @param entry_offset_byte_rtn is a pointer specifying a location to store the byte of the returned aesd_buffer_entry
 *      buffptr member corresponding to char_offset.  This value is only set when a matching char_offset is found
 *      in aesd_buffer.
 * @return the struct aesd_buffer_entry structure representing the position described by char_offset, or
 * NULL if this position is not available in the buffer (not enough data is written).
 */
struct aesd_buffer_entry *aesd_circular_buffer_find_entry_offset_for_fpos(struct aesd_circular_buffer *buffer,
            size_t char_offset, size_t *entry_offset_byte_rtn )
{

    struct aesd_buffer_entry* ret_entry = NULL;
    struct aesd_buffer_entry* ptr_entry = NULL;
    uint8_t index = 0;
    uint32_t accumulated_size = 0;

    for(index=0, ptr_entry=&((buffer)->entry[(index + buffer->out_offs) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED]);
        index<AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;
        index++, ptr_entry=&((buffer)->entry[(index + buffer->out_offs) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED]))
    {

        if ((accumulated_size + ptr_entry->size -1) >= char_offset)
        {
            ret_entry = ptr_entry;
            *entry_offset_byte_rtn = char_offset - accumulated_size;
            break;
        }
        accumulated_size += ptr_entry->size;
        
    }

    return ret_entry;
}

/**
* Adds entry @param add_entry to @param buffer in the location specified in buffer->in_offs.
* If the buffer was already full, overwrites the oldest entry and advances buffer->out_offs to the
* new start location.
* Any necessary locking must be handled by the caller
* Any memory referenced in @param add_entry must be allocated by and/or must have a lifetime managed by the caller.
*/
void aesd_circular_buffer_add_entry(struct aesd_circular_buffer *buffer, const struct aesd_buffer_entry *add_entry)
{
    // insert the entry at the location pointed to by the in_offs
    memcpy(&(buffer->entry[buffer->in_offs]), add_entry, sizeof(struct aesd_buffer_entry));
    // update the in_offs
    buffer->in_offs++;
    buffer->in_offs = buffer->in_offs % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;
    if (buffer->in_offs == 0)
    {
        //overflow
        buffer->full = true;
    }
    // update the out_offs if necessary
    if (buffer->full)
    {
        // This case at which the buffer is currently full
        buffer->out_offs = buffer->in_offs;
    }
}

/**
* Initializes the circular buffer described by @param buffer to an empty struct
*/
void aesd_circular_buffer_init(struct aesd_circular_buffer *buffer)
{
    memset(buffer,0,sizeof(struct aesd_circular_buffer));
}
