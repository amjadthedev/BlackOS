/* =============================================================================
 *  Black OS - auth.c : password authentication & user accounts
 * ========================================================================== */
#include "auth.h"
#include "string.h"
#include "printk.h"
#include "idt.h"

static struct auth_user users[AUTH_USER_MAX];
static int user_count = 0;
static int locked = 1;                 /* boot locked until first unlock   */
static char current_user[AUTH_NAME_MAX];

static u32 fnv_offset = 0xcbf29ce484222325ULL;
static u32 fnv_prime  = 0x100000001b3ULL;

/* FNV-1a 128-bit over (salt || password).  Stored as two u64s in `hash`.     */
static void auth_hash(const u8 *salt, u8 salt_len,
                      const char *pass, u8 *out)
{
    u64 h0 = fnv_offset, h1 = fnv_offset;
    for (u32 i = 0; i < salt_len; i++) {
        h0 ^= salt[i];
        h0 *= fnv_prime;
        h1 ^= salt[i];
        h1 *= fnv_prime;
    }
    for (; *pass; pass++) {
        h0 ^= (u8)*pass;
        h0 *= fnv_prime;
        h1 ^= (u8)*pass;
        h1 *= fnv_prime;
    }
    memcpy(out + 0, &h0, 8);
    memcpy(out + 8, &h1, 8);
}

static int find_user(const char *name)
{
    for (int i = 0; i < user_count; i++)
        if (strcmp(users[i].name, name) == 0)
            return i;
    return -1;
}

void auth_init(void)
{
    memset(users, 0, sizeof(users));
    user_count = 0;
    locked = 1;
    current_user[0] = '\0';

    /* default user "black", no password                                      */
    struct auth_user *u = &users[user_count++];
    strncpy(u->name, "black", sizeof(u->name) - 1);
    u->name[sizeof(u->name) - 1] = '\0';
    for (int i = 0; i < AUTH_SALT_LEN; i++) u->salt[i] = (u8)(0x5A + i);
    u->pass_len = 0;
    u->admin = 1;
    u->created_ms = (u32)uptime_ms;
    auth_hash(u->salt, AUTH_SALT_LEN, "", u->hash);
}

int auth_login(const char *user, const char *pass)
{
    int idx = find_user(user);
    if (idx < 0)
        return 0;
    struct auth_user *u = &users[idx];
    if (u->pass_len == 0) {           /* no password set: auto-accept   */
        strncpy(current_user, user, sizeof(current_user) - 1);
        return 1;
    }
    u8 hash[AUTH_HASH_LEN];
    auth_hash(u->salt, AUTH_SALT_LEN, pass, hash);
    if (memcmp(hash, u->hash, AUTH_HASH_LEN) == 0) {
        strncpy(current_user, user, sizeof(current_user) - 1);
        return 1;
    }
    return 0;
}

int auth_set_password(const char *user, const char *pass)
{
    int idx = find_user(user);
    if (idx < 0)
        return -1;
    struct auth_user *u = &users[idx];
    u8 salt[AUTH_SALT_LEN];
    for (int i = 0; i < AUTH_SALT_LEN; i++)
        salt[i] = (u8)(0x37 + i * 7 + (u8)uptime_ms);
    memcpy(u->salt, salt, AUTH_SALT_LEN);
    auth_hash(salt, AUTH_SALT_LEN, pass, u->hash);
    u->pass_len = (u8)strlen(pass);
    if (u->pass_len > AUTH_PASS_MAX)
        u->pass_len = AUTH_PASS_MAX;
    return 0;
}

int auth_is_locked(void)   { return locked; }
void auth_lock(void)       { locked = 1; }
void auth_unlock(void)     { locked = 0; }

int auth_verify_password(const char *user, const char *pass)
{
    return auth_login(user, pass);
}

const char *auth_current_user(void)
{
    return current_user[0] ? current_user : "black";
}
