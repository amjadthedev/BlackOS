/* =============================================================================
 *  Black OS - auth.h / auth.c : password authentication & user accounts
 *
 *  Simple salted FNV-1a 128-bit credential store for the lock screen and
 *  Settings > Account.  One default user ("black") ships with no password;
 *  the user sets one from Settings, after which the lock screen requires it.
 * ========================================================================== */
#ifndef BLACKOS_AUTH_H
#define BLACKOS_AUTH_H

#include "types.h"

#define AUTH_USER_MAX     4
#define AUTH_NAME_MAX     32
#define AUTH_PASS_MAX     32
#define AUTH_SALT_LEN     8
#define AUTH_HASH_LEN     16

struct auth_user {
    char  name[AUTH_NAME_MAX];
    u8    salt[AUTH_SALT_LEN];
    u8    hash[AUTH_HASH_LEN];
    u8    pass_len;            /* length of the last-set password (0 = none) */
    int   admin;
    u32   created_ms;
};

void   auth_init(void);                         /* default user + no password   */
int    auth_login(const char *user, const char *pass);   /* 1 = ok, 0 = fail             */
int    auth_set_password(const char *user, const char *pass);
int    auth_is_locked(void);
void   auth_lock(void);
void   auth_unlock(void);
int    auth_verify_password(const char *user, const char *pass);
const char *auth_current_user(void);

#endif /* BLACKOS_AUTH_H */