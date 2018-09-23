#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <stdarg.h>
#include "VBoxAuth.h"



static int write_log(const char *format, ...) {
	static FILE* log = NULL;
	if(log == NULL) {
		log = fopen("/var/log/lab-vbox-auth", "a");
	}
	if(log != NULL) {
		va_list args;
		va_start(args, format);
		vfprintf(log, format, args);
		fprintf(log, "\n");
		fflush(log);
		va_end(args);
	}
}


static int execute(const unsigned char* uuid, const char* username, const char* password) {
	char machineEnv[13 + 36 + 1];
	char usernameEnv[14 + strlen(username) + 1];
	char passwordEnv[14 + strlen(password) + 1];

	// For some reason, we have to reverse the bytes in 3 first blocks in UUID
	sprintf(machineEnv, "VBOX_MACHINE=%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
		uuid[3], uuid[2], uuid[1], uuid[0], uuid[5], uuid[4], uuid[7], uuid[6],
		uuid[8], uuid[9], uuid[10], uuid[11], uuid[12], uuid[13], uuid[14], uuid[15]);
	sprintf(usernameEnv, "VBOX_USERNAME=%s", username);
	sprintf(passwordEnv, "VBOX_PASSWORD=%s", password);

	char* env[] = { machineEnv, usernameEnv, passwordEnv, NULL };

	write_log("Authenticating: %s    %s  %s", machineEnv + 13, usernameEnv + 14, passwordEnv + 14);

	if(execle("/usr/bin/env", "env", "lab-vbox-auth", NULL, env) == -1) {
		write_log("ERROR: Failed to execute subprocess: (%d) %s", errno, strerror(errno));
		return -1;
	}

	return 0;
}


static int forkAndExecute(const char* machine, const char* username, const char* password) {
	int link[2];
	pid_t pid;

	if(pipe(link) == -1) {
		write_log("ERROR: Failed to create pipe: (%d) %s", errno, strerror(errno));
		return -1;
	}

	if((pid = fork()) == -1) {
		write_log("ERROR: Failed to fork process: (%d) %s", errno, strerror(errno));
		return -1;
	}

	if(pid == 0) {
		// child process
		dup2(link[1], STDOUT_FILENO);
		close(link[0]);
		close(link[1]);
		return execute(machine, username, password);
	} else {
		// parent process
		close(link[1]);
		// read single byte, which should be Y if authorized
		// or N if not authorized
		char result;
		int nbytes = read(link[0], &result, 1);
		if(nbytes == 0) {
			write_log("ERROR: Failed to read process output: EOF");
			return -1;
		} else if(nbytes < 0) {
			write_log("ERROR: Failed to read process output: (%d) %s", errno, strerror(errno));
			// Failed to read output
			return -1;
		}
		return result == 'Y' ? 0 : 1;
	}
}



AuthResult AuthEntry(const char* szCaller,
                     PAUTHUUID pUuid,
                     AuthGuestJudgement guestJudgement,
                     const char* username,
                     const char* password,
                     const char* domain,
                     int fLogon,
                     unsigned clientId) {

	if(fLogon && pUuid) {
		if(forkAndExecute(*pUuid, username, password) == 0) {
			return AuthResultAccessGranted;
		}
	}

	return AuthResultAccessDenied;
}

