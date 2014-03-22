#ifndef __ACL_H
#define __ACL_H

#include "jasmine.h"
#if OPTION_ACL

user_id_t acl_skey2uid(UINT32 const skey);

BOOL8 acl_authenticate(user_id_t const uid, vp_t const vp);
void acl_authorize(user_id_t const uid, vp_t const vp);

#endif
#endif
