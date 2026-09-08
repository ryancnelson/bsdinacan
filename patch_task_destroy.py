import re

with open("src/core.c", "r") as f:
    text = f.read()

old_destroy = """    if (task->execution != NULL)
        cb_executor_instance_destroy(task->execution);
    for (i = 0; i < CB_MAX_FDS; ++i) {"""

new_destroy = """    if (task->execution != NULL)
        cb_executor_instance_destroy(task->execution);
    if (task->executable_node != NULL)
        cb_vfs_node_release(task->executable_node);
    if (task->pending_executable_node != NULL)
        cb_vfs_node_release(task->pending_executable_node);
    for (i = 0; i < CB_MAX_FDS; ++i) {"""

text = text.replace(old_destroy, new_destroy)

# pending replacement on exec
old_cb_run = """                cb_executor_instance_destroy(task->execution);
            task->execution = new_execution;
            task->state = CB_TASK_RUNNABLE;"""
new_cb_run = """                cb_executor_instance_destroy(task->execution);
            task->execution = new_execution;
            if (task->executable_node != NULL)
                cb_vfs_node_release(task->executable_node);
            task->executable_node = task->pending_executable_node;
            task->pending_executable_node = NULL;
            task->state = CB_TASK_RUNNABLE;"""
text = text.replace(old_cb_run, new_cb_run)

with open("src/core.c", "w") as f:
    f.write(text)

with open("src/shell.c", "r") as f:
    text = f.read()

# Shell migration: don't iterate programs
old_shell = """        for (i = 0; i < kernel->program_count; ++i) {
            if (strcmp(kernel->programs[i]->name, command) == 0) {
                found = 1;
                break;
            }
        }"""
new_shell = """        struct cb_vfs_node *node;
        char path[CB_PATH_MAX];
        struct cb_stat_v1 statbuf;
        
        if (strchr(command, '/') == NULL)
            snprintf(path, sizeof(path), "/bin/%s", command);
        else
            snprintf(path, sizeof(path), "%s", command);
            
        if (cb_vfs_lookup_node(task, path, &node) == 0) {
            if (node->ops->stat(node, &statbuf) == 0 && statbuf.type == CB_NODE_EXECUTABLE) {
                found = 1;
            }
        }"""
text = text.replace(old_shell, new_shell)
with open("src/shell.c", "w") as f:
    f.write(text)

