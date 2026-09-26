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
#define SECURITY_OPERATION_RESOURCE_ACCESS 2U

/*
 * Security capability/status bits exposed to native
 * security utilities through the kernel API.
 */
#define SECURITY_STATUS_KERNEL_PROTECTION     0x01U
#define SECURITY_STATUS_USER_ISOLATION        0x02U
#define SECURITY_STATUS_AUDIT_SUBSYSTEM       0x04U
#define SECURITY_STATUS_FILESYSTEM_ACCESS     0x08U
#define SECURITY_STATUS_FILESYSTEM_INTEGRITY  0x10U
#define SECURITY_STATUS_SYSTEM_PROTECTION     0x20U

#define SECURITY_STATUS_MASK                  0x3FU

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
 * Check whether a caller may access a resource owned by
 * another process.
 *
 * Kernel callers may access any resource.
 * User callers may access resources owned by their own PID.
 *
 * Returns:
 *   SECURITY_ALLOWED
 *   SECURITY_DENIED
 */
unsigned int security_resource_authorize(
    unsigned int caller_pid,
    unsigned int caller_privilege,
    unsigned int resource_owner_pid
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

/*
 * Return the currently implemented security capability mask.
 */
unsigned int security_status(void);

#endif
