#include "security.h"

/*
 * Small fixed-size audit buffer.
 *
 * This is intentionally simple for the first security milestone.
 * Later it can be replaced or extended with persistent audit
 * storage once the filesystem path is ready.
 */
static security_audit_event_t audit_events[
    SECURITY_MAX_AUDIT_EVENTS
];

static unsigned int audit_count = 0;
static unsigned int audit_head = 0;
static unsigned int audit_sequence = 0;

static void security_audit_record(
    unsigned int pid,
    unsigned int privilege,
    unsigned int operation,
    unsigned int result
)
{
    unsigned int position;

    /*
     * Before the buffer becomes full, append normally.
     */
    if (audit_count < SECURITY_MAX_AUDIT_EVENTS)
    {
        position =
            (audit_head + audit_count) %
            SECURITY_MAX_AUDIT_EVENTS;

        audit_count++;
    }
    else
    {
        /*
         * Once full, overwrite the oldest event and move
         * the head forward.
         */
        position =
            audit_head;

        audit_head =
            (audit_head + 1U) %
            SECURITY_MAX_AUDIT_EVENTS;
    }

    audit_sequence++;

    audit_events[position].sequence =
        audit_sequence;

    audit_events[position].pid =
        pid;

    audit_events[position].privilege =
        privilege;

    audit_events[position].operation =
        operation;

    audit_events[position].result =
        result;
}

void security_init(void)
{
    unsigned int i;

    for (i = 0;
         i < SECURITY_MAX_AUDIT_EVENTS;
         i++)
    {
        audit_events[i].sequence = 0;
        audit_events[i].pid = 0;
        audit_events[i].privilege = 0;
        audit_events[i].operation = 0;
        audit_events[i].result = 0;
    }

    audit_count = 0;
    audit_head = 0;
    audit_sequence = 0;
}

unsigned int security_authorize(
    unsigned int pid,
    unsigned int caller_privilege,
    unsigned int operation,
    unsigned int required_privilege
)
{
    unsigned int result;

    /*
     * In InitraOS, lower privilege numbers are stronger.
     *
     * Example:
     *
     *   caller 0, required 0 -> ALLOW
     *   caller 3, required 0 -> DENY
     */
    if (caller_privilege <= required_privilege)
    {
        result =
            SECURITY_ALLOWED;
    }
    else
    {
        result =
            SECURITY_DENIED;
    }

    /*
     * Every authorization decision becomes an audit event.
     */
    security_audit_record(
        pid,
        caller_privilege,
        operation,
        result
    );

    return result;
}
unsigned int security_resource_authorize(
    unsigned int caller_pid,
    unsigned int caller_privilege,
    unsigned int resource_owner_pid
)
{
    unsigned int result;

    /*
     * Kernel privilege can access any process resource.
     */
    if (caller_privilege <= SECURITY_PRIVILEGE_KERNEL)
    {
        result = SECURITY_ALLOWED;
    }
    /*
     * A user process may access its own resource.
     */
    else if (caller_pid == resource_owner_pid)
    {
        result = SECURITY_ALLOWED;
    }
    else
    {
        result = SECURITY_DENIED;
    }

    /*
     * Every resource authorization decision becomes
     * an audit event.
     */
    security_audit_record(
        caller_pid,
        caller_privilege,
        SECURITY_OPERATION_RESOURCE_ACCESS,
        result
    );

    return result;
}

unsigned int security_audit_count(void)
{
    return audit_count;
}

unsigned int security_status(void)
{
    /*
     * These capabilities are implemented by the current
     * 32-bit security architecture. Network security is
     * intentionally not represented here and belongs to
     * the later network-security stage.
     */
    return
        SECURITY_STATUS_KERNEL_PROTECTION |
        SECURITY_STATUS_USER_ISOLATION |
        SECURITY_STATUS_AUDIT_SUBSYSTEM |
        SECURITY_STATUS_FILESYSTEM_ACCESS |
        SECURITY_STATUS_FILESYSTEM_INTEGRITY |
        SECURITY_STATUS_SYSTEM_PROTECTION;
}

int security_audit_get_filtered(
    unsigned int filter_type,
    unsigned int filter_value,
    unsigned int index,
    security_audit_event_t *event
)
{
    unsigned int position;
    unsigned int matched = 0;

    if (event == 0 ||
        filter_type > SECURITY_AUDIT_FILTER_OPERATION)
    {
        return 0;
    }

    for (unsigned int offset = 0;
         offset < audit_count;
         offset++)
    {
        position =
            (audit_head + offset) %
            SECURITY_MAX_AUDIT_EVENTS;

        if (filter_type == SECURITY_AUDIT_FILTER_RESULT &&
            audit_events[position].result !=
                filter_value)
        {
            continue;
        }

        if (filter_type == SECURITY_AUDIT_FILTER_OPERATION &&
            audit_events[position].operation !=
                filter_value)
        {
            continue;
        }

        if (matched == index)
        {
            *event =
                audit_events[position];

            return 1;
        }

        matched++;
    }

    return 0;
}

int security_audit_get(
    unsigned int index,
    security_audit_event_t *event
)
{
    unsigned int position;

    if (event == 0 ||
        index >= audit_count)
    {
        return 0;
    }

    position =
        (audit_head + index) %
        SECURITY_MAX_AUDIT_EVENTS;

    *event =
        audit_events[position];

    return 1;
}
