// TODO: Use this file for helper functions (especially those you want available
// to both executables.

   // Example: Something like the function below might be useful

   // Find and return the location of the first newline character in a string
   // First argument is a string, second argument is the length of the string
   // int find_newline(const char *buf, int len);

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h> 
#include <arpa/inet.h>
#include <errno.h>

#ifndef PORT
  #define PORT 55555
#endif

#ifndef MAX_JOBS
    #define MAX_JOBS 32
#endif

// No paths or lines may be larger than the BUFSIZE below
#define BUF_SIZE 256

#define CMD_INVALID -1
typedef enum {CMD_LISTJOBS, CMD_RUNJOB, CMD_KILLJOB, CMD_WATCHJOB, CMD_EXIT} JobCommand;
//static const int n_job_commands = 5;
// See here for explanation of enums in C: https://www.geeksforgeeks.org/enumeration-enum-c/

typedef enum {NEWLINE_CRLF, NEWLINE_LF} NewlineType;

#define PIPE_READ 0
#define PIPE_WRITE 1

struct job_buffer {
	char buf[BUF_SIZE];
	int consumed;
	int inbuf;
	char *after;
};
typedef struct job_buffer Buffer;

struct client {
	int socket_fd;
	struct job_buffer buffer;
};
typedef struct client Client;

struct watcher_node {
	int client_fd;
	struct watcher_node *next;
};
typedef struct watcher_node WatcherNode;

struct watcher_list {
	struct watcher_node* first;
	int count;
};
typedef struct watcher_list WatcherList;

struct job_node {
	int pid;
	int stdout_fd;
	int stderr_fd;
	int dead;
	int wait_status;
	struct job_buffer stdout_buffer;
	struct job_buffer stderr_buffer;
	struct watcher_list watcher_list;
	struct job_node* next;
};
typedef struct job_node JobNode;


struct job_list {
	struct job_node* first;
	int count;
};
typedef struct job_list JobList;

/* Returns the specific JobCommand enum value related to the
 * input str. Returns CMD_INVALID if no match is found.
 */
JobCommand get_job_command(char* com) {
	for (int i = CMD_LISTJOBS; i <= CMD_EXIT; i++) {
		if (i == (JobCommand)com) {
			return i;
		}
	}
	return CMD_INVALID;
}

/* Forks the process and launches a job executable. Allocates a
 * JobNode containing PID, stdout and stderr pipes, and returns
 * it. Returns NULL if the JobNode could not be created.
 */
JobNode* start_job(char * path, char * const args[]) {
	//printf("Testing %s\n", args);
	JobNode *job = malloc(sizeof(JobNode));
	WatcherList w_list;
	w_list.first = NULL;
	job->watcher_list = w_list;
	job->dead = 0;
	job->wait_status = 0;
	job->stdout_buffer.inbuf = 0;
	job->stdout_buffer.consumed = sizeof(job->stdout_buffer.buf);
	job->stdout_buffer.after = job->stdout_buffer.buf;
	job->stderr_buffer.inbuf = 0;
	job->stderr_buffer.consumed = sizeof(job->stderr_buffer.buf);
	job->stderr_buffer.after = job->stderr_buffer.buf;
	job->next = NULL;
	int stdout_fd[2];
	int stderr_fd[2];
	if (pipe(stdout_fd) == -1) {
		perror("pipe");
		exit(1);
	}
	if (pipe(stderr_fd) == -1) {
		perror("pipe");
		exit(1);
	}
	job->pid = fork();
	if (job->pid > 0) {
		close(stdout_fd[PIPE_WRITE]);
		close(stderr_fd[PIPE_WRITE]);
		job->stdout_fd = stdout_fd[PIPE_READ];
		job->stderr_fd = stderr_fd[PIPE_READ];
	}
	if (job->pid == 0) {
		close(stdout_fd[PIPE_READ]);
		close(stderr_fd[PIPE_READ]);
		dup2(stdout_fd[PIPE_WRITE], fileno(stdout));
		dup2(stderr_fd[PIPE_WRITE], fileno(stderr));
    	execv(path, args);
    	perror("Failed to execute job");
    	exit(1);
	}
	return job;
}

/* Adds the given job to the given list of jobs.
 * Returns 0 on success, -1 otherwise.
 */
int add_job(JobList* j_list, JobNode* node) {
	JobNode *job = j_list->first;
	JobNode *temp = NULL;
	if (j_list->first == NULL) {
		j_list->first = node;
		j_list->count++;
		return 0;
	}
	while (job != NULL) {
		temp = job;
		job = job->next;
	}
	temp->next = node;
	j_list->count++;
	return 0;
}

/* Sends SIGKILL to the given job_pid only if it is part of the given
 * job list. Returns 0 if successful, 1 if it is not found, or -1 if
 * the kill command failed.
 */
int kill_job(JobList* j_list, int pid) {
	JobNode *job = j_list->first;
	JobNode *temp = NULL;
	while (job != NULL) {
		temp = job;
		job = job->next;
		if (temp->pid == pid) {
			if (kill(pid, 9) == -1) {
				return -1;
			}
			return 0;
		}
	}
	return 1;
}

/* Removes a job from the given job list and frees it from memory.
 * Returns 0 if successful, or -1 if not found.
 */
int remove_job(JobList* j_list, int pid) {
	JobNode *job = j_list->first;
	JobNode *temp = NULL;
	while (job != NULL) {
		temp = job;
		job = job->next;
		if (temp->pid == pid) {
			j_list->count--;
			j_list->first = job;
			free(temp);
			return 0;
		}
		if (job != NULL && job->pid == pid) {
			if (job->next != NULL) {
				temp->next = job->next;
			}
			else {
				temp->next = NULL;
			}
			j_list->count--;
			free(job);
			return 0;
		}
	}
	return -1;
}

/* Marks a job as dead.
 * Returns 0 on success, or -1 if not found.
 */
int mark_job_dead(JobList* j_list, int pid) {
	JobNode *job = j_list->first;
	JobNode *temp = NULL;
	if (job == NULL) {
		return -1;
	}
	while (job != NULL) {
		temp = job;
		job = job->next;
		if (temp->pid == pid) {
			temp->dead = 1;
			return 0;
		}
		job = job->next;
	}
	return -1;
}

/* Frees all memory held by a job node and its children.
 */
int delete_job_node(JobNode* node) {
	JobNode *temp = NULL;
	while (node != NULL) {
		temp = node;
		node = node->next;
		free(temp);
	}
	return 0;
}

/* Kills all jobs. Return number of jobs in list.
 */
int kill_all_jobs(JobList* j_list) {
	JobNode *job = j_list->first;
	JobNode *temp = NULL;
	while (job != NULL) {
		temp = job;
		job = job->next;
		if (kill(temp->pid, 9) == -1) {
			return -1;
		}
	}
	j_list->count = 0;
	return 0;
}

/* Sends a kill signal to the job specified by job_node.
 * Return 0 on success, 1 if job_node is NULL, or -1 on failure.
 */
int kill_job_node(JobNode* node) {
	if (node == NULL) {
		return 1;
	}
	if (kill(node->pid, 9) == -1) {
		return -1;
	}
	return 0;
}

void get_jobs(JobList *j_list, char* buf) {
	JobNode *job = j_list->first;
	JobNode *temp = NULL;
	char pid[BUF_SIZE];
	snprintf(buf, 10, "[SERVER] ");
	if (job == NULL) strncat(buf, "No currently running jobs", 25);
	while (job != NULL) {
		temp = job;
		job = job->next;
		snprintf(pid, BUF_SIZE, "%d ", temp->pid);
		strncat(buf, pid, BUF_SIZE);
	}
	strncat(buf, "\r\n", 2);
}

/* Removes a watcher from the given watcher list and frees it from memory.
 * Returns 0 if successful, or 1 if not found.
 */
int remove_watcher(WatcherList* w_list, int client_fd) {
	WatcherNode *watcher = w_list->first;
	WatcherNode *temp = NULL;
	while (watcher != NULL) {
		temp = watcher;
		watcher = watcher->next;
		if (temp->client_fd == client_fd) {
			w_list->count--;
			w_list->first = watcher;
			free(temp);
			return 0;
		}
		if (watcher != NULL && watcher->client_fd == client_fd) {
			if (watcher->next != NULL) {
				temp->next = watcher->next;
			}
			else {
				temp->next = NULL;
			}
			w_list->count--;
			free(watcher);
			return 0;
		}
	}
	return -1;
}

/* Adds the given watcher to the given list of watchers.
 * Returns 0 on success, -1 otherwise.
 */
int add_watcher(WatcherList* w_list, int client_fd) {
	WatcherNode *node = malloc(sizeof(WatcherNode));
	node->next = NULL;
	node->client_fd = client_fd;
	WatcherNode *watcher = w_list->first;

	if (watcher == NULL) {
		w_list->first = node;
		w_list->count++;
		return 0;
	}
	for (int i = 0; i < w_list->count; i++) {
		watcher = watcher->next;
	}
	watcher->next = node;
	w_list->count++;
	return 0;
}

/* Removes a client from every watcher list in the given job list.
 */
void remove_client_from_all_watchers(JobList* j_list, int client_fd) {
	JobNode *job = j_list->first;
	JobNode *temp = NULL;
	while (job != NULL) {
		temp = job;
		job = job->next;
		temp->watcher_list.count--;
		remove_watcher(&(temp->watcher_list), client_fd);
	}
}

/* Adds the given watcher to a given job pid.
 * Returns 0 on success, 1 if job was not found, or -1 if watcher could not
 * be allocated.
 */
int add_watcher_by_pid(JobList* j_list, int pid, int client_fd) {
	JobNode *job = j_list->first;
	JobNode *temp = NULL;
	while (job != NULL) {
		temp = job;
		job = job->next;
		if (temp->pid == pid) {
			temp->watcher_list.count++;
			return add_watcher(&(temp->watcher_list), client_fd);
		}
	}
	return 1;
}

/* Removes the given watcher from the list of a given job pid.
 * Returns 0 on success, 1 if job was not found, or 2 if client_fd could
 * not be found in list of watchers.
 */
int remove_watcher_by_pid(JobList *j_list, int pid, int client_fd) {
	JobNode *job = j_list->first;
	JobNode *temp = NULL;
	while (job != NULL) {
		temp = job;
		job = job->next;
		if (temp->pid == pid) {
			return remove_watcher(&(temp->watcher_list), client_fd);
		}
	}
	return 1;
}

/* Frees all memory held by a watcher list and resets it.
 * Returns 0 on success, -1 otherwise.
 */
int empty_watcher_list(WatcherList *w_list) {
	WatcherNode *watcher = w_list->first;
	WatcherNode *temp = NULL;
	while (watcher != NULL) {
		temp = watcher;
		watcher = watcher->next;
		temp->next = NULL;
		free(temp);
	}
	w_list->first = NULL;
	return 0;
}

/* Frees all memory held by a job list and resets it.
 * Returns 0 on success, -1 otherwise.
 */
int empty_job_list(JobList* j_list) {
	JobNode *job = j_list->first;
	JobNode *temp = NULL;
	while (job != NULL) {
		temp = job;
		job = job->next;
		empty_watcher_list(&(job->watcher_list));
		free(temp);
	}
	j_list->first = NULL;
	return 0;
}

/* Frees all memory held by a watcher node and its children.
 */
int delete_watcher_node(WatcherNode *node) {
	WatcherNode *temp = NULL;
	while (node != NULL) {
		temp = node;
		node = node->next;
		temp->next = NULL;
		free(temp);
	}
	return 0;
}

/* Replaces the first '\n' or '\r\n' found in str with a null terminator.
 * Returns the index of the new first null terminator if found, or -1 if
 * not found.
 */
int remove_newline(char *buf, int inbuf) {
	for (int i = 0; i < inbuf; i++) {
    	if  (buf[i-1] == '\r' && buf[i] == '\n') {
    		buf[i-1] = '\0';
    		buf[i] = '\0';
    		return i;
    	}
    	if (buf[i] == '\n') {
    		buf[i] = '\0';
    		return i;
    	}
    }
    return -1;
}

/* Replaces the first '\n' found in str with '\r', then
 * replaces the character after it with '\n'.
 * Returns 1 + index of '\n' on success, -1 if there is no newline,
 * or -2 if there's no space for a new character.
 */
int convert_to_crlf(char *buf, int inbuf) {
	for (int i = 0; i < inbuf; i++) {
        if (buf[i] == '\n') {
        	buf[i] = '\r';
        	buf[i+1] = '\n';
    		return i+1;
		}
    }
    return -1;
}

/* Search the first n characters of buf for a network newline (\r\n).
 * Return one plus the index of the '\n' of the first network newline,
 * or -1 if no network newline is found.
 */
int find_network_newline(const char *buf, int inbuf) {
	for (int i = 0; i < inbuf; i++) {
       if (buf[i] == '\r' && buf[i + 1] == '\n') return i+2;
    }
    return -1;
}

/* Search the first n characters of buf for an unix newline (\n).
 * Return one plus the index of the '\n' of the first network newline,
 * or -1 if no network newline is found.
 */
int find_unix_newline(const char *buf, int inbuf) {
	for (int i = 0; i < inbuf; i++) {
    	if (buf[i] == '\n') {
    		return i+1;
    	}
    }
    return -1;
}

/* Read as much as possible from file descriptor fd into the given buffer.
 * Returns number of bytes read, or 0 if fd closed, or -1 on error.
 */
int read_to_buf(int, Buffer*);

/* Returns a pointer to the next message in the buffer, sets msg_len to
 * the length of characters in the message, with the given newline type.
 * Returns NULL if no message is left.
 */
char* get_next_msg(Buffer*, int*, NewlineType);

/* Removes consumed characters from the buffer and shifts the rest
 * to make space for new characters.
 */
void shift_buffer(Buffer *);

/* Returns 1 if buffer is full, 0 otherwise.
 */
int is_buffer_full(Buffer *);
