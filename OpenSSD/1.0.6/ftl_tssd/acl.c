#include "acl.h"
#if OPTION_ACL
#include "sot.h"

uid_t	acl_skey2uid(UINT32 const skey) {
	return (uid_t)skey;
}

#endif

