#ifndef __SLAB_H
#define __SLAB_H
/* *
 * Slab
 *
 * Preallocated contiguous memory for efficient allocation and deallocation
 * for objects of specific type. The concept was borrowed from Linux kernel.
 * */

#define	define_slab_interface(name, type)				\
		static type*	slab_allocate_##name();			\
		static void 	slab_deallocate_##name(type*);

#define define_slab_implementation(name, type, capacity)		\
		typedef struct _slab_##name##_obj {			\
			type				_payload;	\
			struct _slab_##name##_obj	*next_free;	\
		} slab_##name##_obj_t;					\
		static slab_##name##_obj_t	 slab_##name##_buf[capacity];	\
		static UINT32			 slab_##name##_size = 0;	\
		static slab_##name##_obj_t 	*slab_##name##_free_list = NULL;	\
		static UINT32			 slab_##name##_num_free = capacity;	\
		static type* slab_allocate_##name() {			\
			if (slab_##name##_free_list) {			\
				slab_##name##_obj_t	*free_obj;	\
				free_obj = slab_##name##_free_list;	\
				slab_##name##_free_list = free_obj->next_free;	\
				slab_##name##_num_free--;		\
				return (type*) free_obj;		\
			}						\
			if (slab_##name##_size == capacity) return NULL;	\
			slab_##name##_num_free--;			\
			return (type*) & (slab_##name##_buf[slab_##name##_size++]);	\
		}							\
		static void slab_deallocate_##name(type* obj) {		\
			slab_##name##_obj_t *slab_obj = 		\
					(slab_##name##_obj_t*) obj;	\
			slab_obj->next_free = slab_##name##_free_list;	\
			slab_##name##_free_list = slab_obj;		\
			slab_##name##_num_free++;			\
		}
#endif
