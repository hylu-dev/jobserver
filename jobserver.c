#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h> 
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <signal.h>
#include <errno.h>

#include "socket.h"
#include "jobprotocol.h"
#define MAX_CLIENTS 20
#define QUEUE_LENGTH 5

#ifndef JOBS_DIR
#define JOBS_DIR "jobs/"
#endif

//includes code from t11
//includes code from t12

Client client[MAX_CLIENTS];

JobList j_list;

Buffer buffer;


// Flag to keep track of SIGINT received
int sigint_received;
struct stat stat_buf;
int status;

int setup_new_client(int fd, Client *clients) {
	int user_index = 0;
	while (user_index < MAX_CLIENTS && clients[user_index].socket_fd != -1) {
		user_index++;
	}

	int client_fd = accept_connection(fd);
	if (client_fd < 0) {
		return -1;
	}

	if (user_index >= MAX_CLIENTS) {
		fprintf(stderr, "server: max concurrent connections\n");
		close(client_fd);
		return -1;
	}
	clients[user_index].socket_fd = client_fd;
	return client_fd;
}

int remove_client(int fd, int client_index, Client *clients, JobList *j_list) {
	return 0;
}

int process_client_request(Client *client, JobList *j_list, fd_set *all_fds) {
	int fd = client->socket_fd;
    int num_read;

    //buffer
	if ((num_read = read(fd, buffer.buf, buffer.consumed)) > 0) {
		buffer.inbuf += num_read;
		int where;
		if ((where = remove_newline(buffer.buf, buffer.inbuf) > 0)) {
			printf("%s\n", buffer.buf);
			char *buf_tok;
			char *rest = buffer.buf;
			buf_tok = strtok_r(rest, " ", &rest);
			// Perform given job
		    if (!strncmp(buf_tok, "jobs", strlen(buf_tok))) {
		    	char message[BUF_SIZE];
		    	get_jobs(j_list, message);
		    	write(fd, message, strlen(message));
		    	printf("%s", message);
		    }
		    else if (!strncmp(buf_tok, "run", strlen(buf_tok))) {
		    	char *job = strtok_r(rest, " ", &rest);
		    	char exe_file[BUF_SIZE];
		    	strncpy(exe_file, job, BUF_SIZE);
		    	snprintf(exe_file, BUF_SIZE, "%s%s", JOBS_DIR, job);
		    	//check if exists
		    	if (lstat(exe_file, &stat_buf) == -1) {
					fprintf(stderr, "file does not exist\n");
					memset(buffer.buf, '\0', where);
					buffer.inbuf -= where;
					memmove(buffer.buf, buffer.buf+where, buffer.inbuf);
					return 0;
				}
				// get amount of args
				char* num_rest = rest;
				int num_args = 2;
				while (strtok_r(num_rest, " ", &num_rest)) {
					num_args++;
				}

				//ideally pass it in but not quite working
		    	char *args[num_args];
		    	args[num_args-1] = '\0';
		    	args[0] = job;
		    	int i = 1;
				while ((buf_tok = strtok_r(rest, " ", &rest))) {
					args[i] = buf_tok;
					i++;
				}
				// check if at max jobs
				if (j_list->count >= MAX_JOBS) {
					char message[BUF_SIZE];
		    		snprintf(message, BUF_SIZE, "[SERVER] MAXJOBS exceeded\r\n");
		    		write(fd, message, strlen(message));
				} else {
					JobNode *node = start_job(exe_file, args);
					waitpid(node->pid, NULL, WNOHANG);
					if (WEXITSTATUS(status) == 0) {
						char message[BUF_SIZE];
			    		snprintf(message, BUF_SIZE, "[SERVER] Job %d created\r\n", node->pid);
			    		write(fd, message, strlen(message));
						printf("[SERVER] Job %d created\n", node->pid);
					} else {
						node->wait_status = WEXITSTATUS(status);
					}
					add_watcher(&(node->watcher_list), fd);
					FD_SET(node->stdout_fd, all_fds);
					FD_SET(node->stderr_fd, all_fds);
					add_job(j_list, node);
				}
		    }
		    else if (!strncmp(buf_tok, "kill", strlen(buf_tok))) {
		    	char message[BUF_SIZE+23];
		    	if ((buf_tok = strtok_r(rest, " ", &rest))) {
		    		int pid = strtol(buf_tok, NULL, 10);
			    	char message[BUF_SIZE];
			    	snprintf(message, BUF_SIZE, "[SERVER] Job %d not found\r\n", pid);
			    	if (kill_job(j_list, pid) == 1) {
			    		write(fd, message, strlen(message));
			    		printf("[SERVER] Job %d not found\n", pid);
			    	}
		    	} else {
	    			snprintf(message, BUF_SIZE, "[SERVER] Invalid command: kill\r\n");
	    			printf("[SERVER] Invalid command: kill\n");
	    			write(fd, message, strlen(message));
		    	}
		    }
		    else if (!strncmp(buf_tok, "watch", strlen(buf_tok))) {
		    	char message[BUF_SIZE];
		    	if ((buf_tok = strtok_r(rest, " ", &rest))) {
			    	int pid = strtol(buf_tok, NULL, 10);
		    		if (remove_watcher_by_pid(j_list, pid, client->socket_fd) == 0) {
		    			snprintf(message, BUF_SIZE, "[SERVER] No longer watching Job %d\r\n", pid);
		    			printf("[SERVER] longer watching Job %d\n", pid);
		    			write(fd, message, strlen(message));
		    		} 
		    		else if (add_watcher_by_pid(j_list, pid, client->socket_fd) == 0) {
		    			snprintf(message, BUF_SIZE, "[SERVER] Watching job %d\r\n", pid);
		    			printf("[SERVER] Watching job %d\n", pid);
		    			write(fd, message, strlen(message));
		    		} else {
		    			snprintf(message, BUF_SIZE, "[SERVER] Job %d not found\r\n", pid);
		    			printf("[SERVER] Job %d not found\n", pid);
		    			write(fd, message, strlen(message));
		    		}
			    }	
			    else {
	    			snprintf(message, BUF_SIZE, "[SERVER] Invalid command: watch\r\n");
	    			printf("[SERVER] Invalid command: watch\n");
	    			write(fd, message, strlen(message));
	    		}
			}	
			else {
			    printf("[SERVER] Invalid command: %s\n", buf_tok);
			}
			memset(buffer.buf, '\0', where);
			buffer.inbuf -= where;
			memmove(buffer.buf, buffer.buf+where, buffer.inbuf);
			}
			//buffer.after = buffer.buf+buffer.inbuf;
	        buffer.consumed = sizeof(buffer.buf)-buffer.inbuf;
		}
		if (num_read == 0) {
			client->socket_fd = -1;
			return fd;
		}
	return 0;
}

void process_job_output(JobNode *node, int fd, Buffer *buffer) {	
	int num_read;
	char format[BUF_SIZE] = "";
	int count = 0;
	int len_pid = node->pid;
	//get length of pid
	while (len_pid != 0) {
        len_pid /= 10;
		count++;
    }
    int form = 7+count;

	if ((num_read = read(fd, buffer->after, buffer->consumed)) > 0)  {
		buffer->inbuf += num_read;
		int where;
		// check buffer overflow	
	    if (buffer->inbuf >= BUF_SIZE) {
	    	node->dead = 1;
			char message[BUF_SIZE];
			snprintf(message, BUF_SIZE, "*(SERVER)* Buffer from job %d is full. Aborting Job.\r\n", node->pid);
			node->wait_status = 1;
			write(node->watcher_list.first->client_fd, message, strlen(message));
	    }
		while ((where = find_unix_newline(buffer->buf, buffer->inbuf)) > 0) {
			WatcherNode *watcher = node->watcher_list.first;
			WatcherNode *temp = NULL;
			buffer->buf[where-1] = '\0';
			while (watcher != NULL) {
				temp = watcher;
				watcher = watcher->next;
			if (fd == node->stdout_fd) {
				snprintf(format, BUF_SIZE+form, "[JOB %d] %s", node->pid, buffer->buf);
			}
			else if (fd == node->stderr_fd) {
				form += 2;
				snprintf(format, BUF_SIZE+form, "*(JOB %d)* %s", node->pid, buffer->buf);
			}
			printf("%s\n",format);
			format[where+form-1] = '\r';
			format[where+form] = '\n';
				write(temp->client_fd, format, where+form+1);
			}
			memset(buffer->buf, '\0', where);
			buffer->inbuf -= where;
			memmove(buffer->buf, buffer->buf+where, buffer->inbuf);
		}
		buffer->after = buffer->buf+buffer->inbuf;
		buffer->consumed = sizeof(buffer->buf)-buffer->inbuf;
	}
}

int process_dead_children(JobList *j_list, fd_set *all_fds) {
	JobNode *job = j_list->first;
	JobNode *temp = NULL;
	while (job != NULL) {
		temp = job;
		job = job->next;
		if (temp->dead == 1) {
			FD_CLR(temp->stdout_fd, all_fds);
			FD_CLR(temp->stderr_fd, all_fds);
			close(temp->stdout_fd);
			close(temp->stderr_fd);
			char message[BUF_SIZE];
			if (temp->wait_status == 0) {
	    		snprintf(message, BUF_SIZE, "[JOB %d] Exited due to signal\r\n", temp->pid);
	    		printf("[JOB %d] Exited due to signal\r\n", temp->pid);
	    	} else {
	    		snprintf(message, BUF_SIZE, "[JOB %d] Exited with status %d\r\n", temp->pid, temp->wait_status); //not quite right
	    		printf("[JOB %d] Exited with status %d\r\n", temp->pid, temp->wait_status);
	    	}

			WatcherNode *watcher = temp->watcher_list.first;
			WatcherNode *w_temp = NULL;
			while (watcher != NULL) {
				w_temp = watcher;
				watcher = watcher->next;
	    		write(w_temp->client_fd, message, strlen(message));
    		}
			remove_job(j_list, temp->pid);
		}
	}
	return 0;
}

/* Process output from each child, remove them if they are dead, announce to watchers.
 * Returns 1 if at least one child exists, 0 otherwise.
 */
int process_jobs(JobList *j_list, fd_set *listen_fds, fd_set *all_fds) {
	JobNode *job = j_list->first;
	JobNode *temp = NULL;
	while (job != NULL) {
		temp = job;
		job = job->next;
		if (FD_ISSET(temp->stdout_fd, listen_fds)) {
			process_job_output(temp, temp->stdout_fd, &(temp->stdout_buffer));
		}
		if (FD_ISSET(temp->stderr_fd, listen_fds)) {
			process_job_output(temp, temp->stderr_fd, &(temp->stderr_buffer));
		}
	}
	process_dead_children(j_list, all_fds);
	return 0;
}

int get_highest_fd(int listen_fd, Client *clients, JobList *j_list) {
	int max_fd = listen_fd;
	for (int index = 0; index < MAX_CLIENTS; index++) {
		if (clients[index].socket_fd > max_fd) {
			max_fd = clients[index].socket_fd; 
		}
	}
	JobNode *job = j_list->first;
	JobNode *temp = NULL;
	while (job != NULL) {
		temp = job;
		job = job->next;
		if (temp->stdout_fd > max_fd) {
			max_fd = temp->stdout_fd; 
		}
		if (temp->stderr_fd > max_fd) {
			max_fd = temp->stderr_fd;
		}
	}
	return max_fd;
}

void clean_exit(int listen_fd, Client *clients, JobList *j_list, int exit_status) {
	char message[BUF_SIZE];
	for (int index = 0; index < MAX_CLIENTS; index++) {
		if (clients[index].socket_fd > -1) {
	    	snprintf(message, BUF_SIZE, "[SERVER] Shutting Down\r\n");
	    	write(clients[index].socket_fd, message, strlen(message));
			close(clients[index].socket_fd);
    	}
	}
	empty_job_list(j_list);
	printf("[SERVER] Shutting Down\n");
	exit(exit_status);
}

/* SIGINT handler:
 * We are just raising the sigint_received flag here. Our program will
 * periodically check to see if this flag has been raised, and any necessary
 * work will be done in main() and/or the helper functions. Write your signal 
 * handlers with care, to avoid issues with async-signal-safety.
 */
void sigint_handler(int code) {	
    sigint_received = 1;
}

void sigchild_handler(int sig, siginfo_t *siginfo, void *context) {
    mark_job_dead(&j_list, siginfo->si_pid);
}



int main(void) {
	sigint_received = 0;
	// SIGINT HANDLER
	struct sigaction sigi;
	sigi.sa_handler = sigint_handler;
    sigi.sa_flags=SA_RESTART;
    sigemptyset(&sigi.sa_mask);
    sigaction(SIGINT, &sigi, NULL);
	// SIGCHLD HANDLER
	struct sigaction newact;
	newact.sa_sigaction = sigchild_handler;
    newact.sa_flags = SA_SIGINFO | SA_RESTART;
    sigemptyset(&newact.sa_mask);
    sigaction(SIGCHLD, &newact, NULL);

	// This line causes stdout and stderr not to be buffered.
	// Don't change this! Necessary for autotesting.
	setbuf(stdout, NULL);
	setbuf(stderr, NULL);	

	struct sockaddr_in *self = init_server_addr(PORT);
	int socket_fd = setup_server_socket(self, QUEUE_LENGTH);


	/* TODO: Initialize job and client tracking structures, starts
	 * accepting connections. Listen for messages from both clients
	 * and jobs. Execute client commands if properly formatted.
	 * Forward messages from jobs to appropriate clients.
	 * Tear down cleanly.
	 */
	Client clients[MAX_CLIENTS];
	j_list.first = NULL;
	j_list.count = 0;
	buffer.inbuf = 0;
	buffer.consumed = sizeof(buffer.buf);
	buffer.after = buffer.buf;

	for (int index = 0; index < MAX_CLIENTS; index++) {
		clients[index].socket_fd = -1;
	}

	int max_fd = socket_fd;
	fd_set all_fds, listen_fds;
	FD_ZERO(&all_fds);
	FD_SET(socket_fd, &all_fds);

	while (sigint_received == 0) {
		listen_fds = all_fds;
		int nready = select(max_fd + 1, &listen_fds, NULL, NULL, NULL);
		if (nready == -1) {
			if (errno = 4) break;
			perror("server: select");
			exit(1);
		}

		// Is it the original socket? Create a new connection ...
		if (FD_ISSET(socket_fd, &listen_fds)) {
			int client_fd = setup_new_client(socket_fd, clients);
			if (client_fd < 0) {
				continue;
			}
			if (client_fd > max_fd) {
				max_fd = client_fd;
			}
			FD_SET(client_fd, &all_fds);
			printf("Accepted connection\n");
		}


		for (int index = 0; index < MAX_CLIENTS; index++) {
			if (clients[index].socket_fd > -1 && FD_ISSET(clients[index].socket_fd, &listen_fds)) {
				// Note: never reduces max_fd
				printf("[CLIENT %d] ", clients[index].socket_fd);
				int client_closed = process_client_request(&clients[index], &j_list, &all_fds);
				if (client_closed > 0) {
					FD_CLR(client_closed, &all_fds);
					close(client_closed);
					printf("Connection Closed\n");
				}
			}
			max_fd = get_highest_fd(socket_fd, clients, &j_list);

		}
		process_jobs(&j_list, &listen_fds, &all_fds);
	}
	free(self);
	clean_exit(socket_fd, clients, &j_list, 0);
	return 0;
}

