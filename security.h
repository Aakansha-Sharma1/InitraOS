#ifndef INITRAOS_SECURITY_H
#define INITRAOS_SECURITY_H

/*
 * InitraOS security core.
 *
 * The current security model uses the existing CPU privilege
 * levels already represented by InitraOS tasks:
 *
 *   0 = kernel
 *   3 = user
 *
 * Lower numeric privilege means greater privilege.
 */

#define SECURITY_PRIVILEGE_KERNEL 0U
#define SECURITY_PRIVILEGE_USER   3U

#define SECURITY_ALLOWED 1U
#define SECURITY_DENIED  0U

#define SECURITY_MAX_AUDIT_EVENTS 16U

#define SECURITY_OPERATION_PROTECTED_TEST 1U

typedef struct security_audit_event
{
    unsigned int sequence;
    unsigned int pid;
    unsigned int privilege;
    unsigned int operation;
    unsigned int result;
} security_audit_event_t;

/*
 * Initialize the security core and clear all audit events.
 */
void security_init(void);

/*
 * Check whether a caller is authorized to perform an operation.
 *
 * Lower privilege numbers represent stronger privilege.
 *
 * Returns:
 *   SECURITY_ALLOWED
 *   SECURITY_DENIED
 */
unsigned int security_authorize(
    unsigned int pid,
    unsigned int caller_privilege,
    unsigned int operation,
    unsigned int required_privilege
);

/*
 * Return the number of currently stored audit events.
 */
unsigned int security_audit_count(void);

/*
 * Read an audit event in chronological order.
 *
 * Returns:
 *   1 = event returned
 *   0 = invalid index
 */
int security_audit_get(
    unsigned int index,
    security_audit_event_t *event
);

#endif
