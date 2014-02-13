#include "acl.h"
#if OPTION_ACL
#include "sot.h"

user_id_t	acl_skey2uid(UINT32 const skey) {
	return (user_id_t)skey;
}

#endif

