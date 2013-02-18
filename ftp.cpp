/*
 * ftp.cpp
 *
 *  Created on: Nov 7, 2012
 *      Author: Michael Plotke
 */

#include <iostream>
#include <fstream>
#include <cstdio>
#include <cstdlib>
#include <sys/types.h>   // Types used in sys/socket.h and netinet/in.h
#include <netinet/in.h>  // Internet domain address structures and functions
#include <sys/socket.h>  // Structures and functions used for socket API
#include <netdb.h>       // Used for domain/DNS hostname lookup
#include <unistd.h>
#include <errno.h>
#include <arpa/inet.h>
#include <string.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <semaphore.h>
#include <sys/time.h>

using namespace std;

// ensures no extra arguments
#define xarg(text)						\
char * bad;								\
bad = strtok(NULL, " \t");				\
if (bad != NULL)						\
{										\
	cout << text << endl;				\
	continue;							\
}

// tests the command connection
#define checkctl()						\
if (cmd_sd == -1)						\
{										\
	cout << "Not connected." << endl;	\
	continue;							\
}

int cmd_sd;
FILE * in, * out;
int data_sd;


/* =================================== trim ========================================
 * removes whitespace from the beginning and end of a given C string.
 */
void trim(char * & command)
{
	unsigned i = (unsigned)strlen(command)-1;
	while (isspace(command[i]))
	{
		command[i] = '\0';
		i--;
	}
	i = 0;
	while (isspace(command[i]))
	{
		command++;
	}
}

/* =================================== readreply ===================================
 * reads and prints a reply from the server returning the reply code and optionally
 * filling a given C string with the final line of the reply.
 */
int readreply(char * reply = NULL)
{
	char code[4]; // the first 3 chars of the first line
	char temp[4]; // the first 3 chars of each line
	char c;
	bool last = false; // last line?
	bool first = true; // first line?

	while (!last) // we still have more lines
	{
		int i = 0; // position on each line

		while ((c = getc(in)) != '\n') // not finished with the line
		{
			if (i < 3)
			{
				temp[i] = c;

				if (first)
				{
					code[i] = c;

					if (reply) // add the code to the reply
					{
						reply[i] = c;
					}
				}
			}
			if (i == 3)
			{
				if (first)
				{
					code[i] = temp[i] = '\0'; // null terminate
				}
				if (c != '-' && strcmp(temp, code) == 0)
				{
					last = true; // see ftp protocol spec
				}
			}
			if (last) // reply only returns the last line
			{
				if (reply)
				{
					reply[i] = c;
				}
			}
			cout << c; // print each character
			i++;
		}
		if (last)
		{
			if (reply)
			{
				reply[i] = '\0'; // null terminate
			}
		}
		cout << '\n';
		first = false;
	}
	return atoi(code); // return the code as an integer
}

/* =================================== openpsv =====================================
 * opens a data connection with the server by means of the PASV command.
 */
void openpsv()
{
	fprintf(out, "PASV\r\n");
	fflush(out);

	//unsigned short code;
	char buff[90]; // buffer for ftp server reply
	char * reply = buff;
	readreply(reply);

	//ignore the garbage, grab the relevant numbers
	uint32_t a0, a1, a2, a3, p0, p1;
	sscanf(reply, "%*u %*s %*s %*s (%u,%u,%u,%u,%u,%u)", &a0, &a1, &a2, &a3, &p0, &p1);


	sockaddr_in sin;
	memset(&sin, '\0', sizeof(sin));   // bzero replacement
	sin.sin_family      = AF_INET;     // Address Family Internet
	// << is a bitwise shift left. 32 bit address in 4 8 bit parts, and 16 bit port in 2 8 bit parts
	sin.sin_addr.s_addr = htonl((a0 << 24) | ((a1 & 0xff) << 16) | ((a2 & 0xff) << 8) | (a3 & 0xff));
	sin.sin_port        = htons(((p0 & 0xff) << 8) | (p1 & 0xff));

	//cerr << a0 << '.' << a1 << '.' << a2 << '.' << a3 << endl;
	//cerr << sin.sin_port << endl;

	if ((data_sd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
	{
		// clean up and continue instead
		perror("socket failed");
		exit(1);
	}

	int err = connect(data_sd, (sockaddr *)&sin, sizeof(sin));
	if (err < 0)
	{
		// clean up and continue instead
		perror("connect failed");
		exit(1);
	}
}

/* =================================== openctl =====================================
 * opens a control connection with the server and prompts for a username and
 * password.
 */
void openctl(char * & hostname, char * & portstr)
{
	int port = atoi(portstr);
	struct hostent * host = gethostbyname(hostname);

	sockaddr_in sin;
	memset(&sin, '\0', sizeof(sin));   // bzero replacement
	sin.sin_family      = AF_INET;     // Address Family Internet
	sin.sin_addr.s_addr = inet_addr(inet_ntoa(*(struct in_addr *) *host->h_addr_list));
	sin.sin_port        = htons(port); // convert host byte-order

	if ((cmd_sd = socket(PF_INET, SOCK_STREAM, 0)) < 0)
	{
		// clean up and continue instead
		perror("socket failed");
		exit(1);
	}
	if (connect(cmd_sd, (sockaddr *)&sin, sizeof(sin)) < 0)
	{
		// clean up and continue instead
		perror("connect failed");
		exit(1);
	}
	in = fdopen(cmd_sd, "r");       // _IO_FILE pointer
	out = fdopen(dup(cmd_sd), "w"); // _IO_FILE pointer
	if (!out || !in)
	{
		perror("fdopen failed");
		exit(1);
	}
	readreply();

	char buff[30]; // buffer for credentials
	char * creds = buff;

	cout << "Name (" << hostname << ':' << getlogin() << "): ";

	creds = buff;
	cin.getline(creds, 30);

	trim(creds);

	fprintf(out, "USER %s\r\n", creds); // USER command
	fflush(out);

	readreply();

	cout << "Password: ";

	creds = buff;
	cin.getline(creds, 30);

	trim(creds);

	fprintf(out, "PASS %s\r\n", creds); // PASS command
	fflush(out);

	readreply();
}

/* =================================== main ========================================
 * checks the arguments, calls openctl, then waits for valid ftp commands.
 */
int main(int argc, char * argv[])
{
	if (argc > 2)
	{
		cerr << "usage: ftp [hostname]" << endl;
		return 1;
	}
	if (argc == 2)
	{
		char * portstr = "21";
		openctl(argv[1], portstr); // open the ftp control connection
	}
	cout << "enter a commmand (try 'help'):" << endl;

	char buff[60]; // buffer for inputed command

	while (true)
	{
		cout << "ftp> ";
		char * input = buff;
		cin.getline(input, 50);

		trim(input);
		char * command;
		command = strtok(input, " \t");
		if (command == NULL)
		{
			continue; // no command...
		}

		if (strcmp(command, "help") == 0) //------------------- HELP ---------------
		{
			xarg("usage: help");
			cout << "open hostname port - Establish a TCP connection to hostname on the specified port" << endl;
			cout << "get filename       - Get a copy of filename from the current remote directory on the server" << endl;
			cout << "put filename       - Store a copy of filename into the current remote directory on the server" << endl;
			cout << "delete filename    - Removes filename from the the current remote directory on the server" << endl;
			cout << "ls                 - Ask the server to send a list of its current directory contents" << endl;
			cout << "cd pathname        - Change the server's current working directory to pathname" << endl;
			cout << "pwd                - Prints the full path of the server's current working directory" << endl;
			cout << "mkdir pathname     - Creates a new directory in a given valid sub-directory" << endl;
			cout << "rmdir pathname     - Destroys a given valid empty directory" << endl;
			cout << "close              - Close the connection but do not quit ftp" << endl;
			cout << "quit               - Close the connection and quit ftp" << endl;
		}
		else if (strcmp(command, "open") == 0) //-------------- OPEN ---------------
		{
			char * hostname, * portstr;
			hostname = strtok(NULL, " \t");
			if (hostname == NULL)
			{
				cout << "usage: open hostname port" << endl;
				continue;
			}
			portstr = strtok(NULL, " \t");
			if (portstr == NULL)
			{
				cout << "usage: open hostname port" << endl;
				continue;
			}
			// #define for handling extra arguments
			xarg("usage: open hostname port");

			openctl(hostname, portstr);
		}
		else if (strcmp(command, "pwd") == 0) //--------------- PWD ----------------
		{
			// #define for checking the control connection
			checkctl();
			xarg("usage: pwd");

			switch(fork()) // see faq q4
			{
			case 0:  // child process
				exit(readreply());
			default: // parent process
				fprintf(out, "PWD\r\n");
				fflush(out);
			}
			int status;
			wait(&status);
		}
		else if (strcmp(command, "mkdir") == 0) //------------- MKDIR --------------
		{
			checkctl();
			char * dir;
			dir = strtok(NULL, " \t");
			if (dir == NULL)
			{
				cout << "usage: mkdir pathname" << endl;
				continue;
			}
			xarg("usage: mkdir pathname");

			switch(fork()) // see faq q4
			{
			case 0:  // child process
				exit(readreply());
			default: // parent process
				fprintf(out, "MKD %s\r\n", dir);
				fflush(out);
			}
			int status;
			wait(&status);
		}
		else if (strcmp(command, "rmdir") == 0) //------------- RMDIR --------------
		{
			checkctl();
			char * dir;
			dir = strtok(NULL, " \t");
			if (dir == NULL)
			{
				cout << "usage: rmdir pathname" << endl;
				continue;
			}
			xarg("usage: rmdir pathname");

			switch(fork()) // see faq q4
			{
			case 0:  // child process
				exit(readreply());
			default: // parent process
				fprintf(out, "RMD %s\r\n", dir);
				fflush(out);
			}
			int status;
			wait(&status);
		}
		else if (strcmp(command, "cd") == 0) //---------------- CD -----------------
		{
			checkctl();
			char * dir;
			dir = strtok(NULL, " \t");
			if (dir == NULL)
			{
				cout << "usage: cd subdir" << endl;
				continue;
			}
			xarg("usage: cd subdir");

			switch(fork()) // see faq q4
			{
			case 0:  // child process
				exit(readreply());
			default: // parent process
				fprintf(out, "CWD %s\r\n", dir);
				fflush(out);
			}
			int status;
			wait(&status);
		}
		else if (strcmp(command, "ls") == 0) //---------------- LS -----------------
		{
			checkctl();
			xarg("usage: ls");

			openpsv(); // for data transfer

			switch(fork()) // see faq q4
			{
			case 0:  // child process
				readreply();
				readreply();
				exit(0);
			default: // parent process
				fprintf(out, "LIST\r\n");
				fflush(out);
			}
			int status;
			wait(&status);

			FILE * lsin = fdopen(data_sd, "r");

			char c;
			while ((c = getc(lsin)) != EOF)
			{
				cout << c; // print the ls reply
			}
		}
		else if (strcmp(command, "get") == 0) //--------------- GET ----------------
		{
			checkctl();
			char * rfile, * lfile = NULL;
			char rbuf[30];
			char lbuf[30];

			rfile = strtok(NULL, " \t");
			// if the remote-file is NULL, query both
			if (rfile == NULL)
			{
				cout << "(remote-file) ";
				rfile = rbuf;
				cin.getline(rfile, 30);

				trim(rfile);

				cout << "(local-file) ";
				lfile = lbuf;
				cin.getline(lfile, 30);

				trim(lfile);
			}
			if (lfile == NULL)
			{
				lfile = strtok(NULL, " \t");
				if (lfile == NULL) // if just the local-file is NULL
				{
					lfile = rfile; // use the same name
				}
			}
			xarg("usage: get remote-file [local-file]");

			openpsv();

			switch(fork()) // see faq q4
			{
			case 0: // child process
				readreply();
				exit(readreply());
			default: // parent process
				fprintf(out, "TYPE I\r\n");
				fflush(out);
				fprintf(out, "RETR %s\r\n", rfile);
				fflush(out);
			}
			int status;
			wait(&status);
			if (status % 10 != 0)
			{
				continue;
			}

			// open a local file with all the right settings
			mode_t mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;
			int fd = open(lfile, O_WRONLY | O_CREAT, mode);

			ssize_t size;
			char * buf = (char *)malloc(100);

			// transfer tracking variables
			struct timeval tv1, tv2;
			unsigned long bytecount = 0;

			gettimeofday(&tv1, NULL);

			while ((size = read(data_sd, buf, 100)) > 0)
			{
				// read from the bytecount socket, and write to
				write(fd, buf, size); // our new file.
				bytecount += size;
			}
			gettimeofday(&tv2, NULL);

			// printf some stats about the transfer
			float seconds = float(tv2.tv_sec - tv1.tv_sec)
					+ (float(tv2.tv_usec - tv1.tv_usec) / 1.0E6);
			printf("%d bytes received in %g seconds (%g Kbytes/s)\n",
					bytecount, seconds, float(bytecount)/seconds/1000.0);

			close(fd);
			close(data_sd);

			readreply();
		}
		else if (strcmp(command, "put") == 0) //--------------- PUT ----------------
		{
			checkctl();
			char * lfile, * rfile = NULL;
			char lbuf[30];
			char rbuf[30];

			lfile = strtok(NULL, " \t");
			if (lfile == NULL)
			{
				cout << "(local-file) ";
				lfile = lbuf;
				cin.getline(lfile, 30);

				trim(lfile);

				cout << "(remote-file) ";
				rfile = rbuf;
				cin.getline(rfile, 30);

				trim(rfile);
			}
			if (rfile == NULL)
			{
				rfile = strtok(NULL, " \t");
				if (rfile == NULL)
				{
					rfile = lfile;
				}
			}
			xarg("usage: get local-file [remote-file]");

			// open a local file with all the right settings
			mode_t mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;
			int fd = open(lfile, O_RDONLY, mode);

			if (fd < 0) // can't find supposed local file
			{
				cerr << "ftp: local: " << lfile << ": No such file or directory" << endl;
				continue;
			}
			openpsv();

			switch(fork()) // see faq q4
			{
			case 0:  // child process
				readreply();
				readreply();
				exit(0);
			default: // parent process
				fprintf(out, "TYPE I\r\n");
				fflush(out);
				fprintf(out, "STOR %s\r\n", rfile);
				fflush(out);
			}
			int status;
			wait(&status);
			if (status % 10 != 0)
			{
				continue;
			}
			ssize_t size;
			char * buf = (char *)malloc(100);

			// transfer tracking variables
			struct timeval tv1, tv2;
			unsigned long bytecount = 0;

			gettimeofday(&tv1, NULL);

			while ((size = read(fd, buf, 100)) > 0)
			{
				// read from our local file, and write to
				write(data_sd, buf, size); // the data socket.
				bytecount += size;
			}
			gettimeofday(&tv2, NULL);

			// printf some stats about the transfer
			float seconds = float(tv2.tv_sec - tv1.tv_sec)
					+ (float(tv2.tv_usec - tv1.tv_usec) / 1.0E6);
			printf("%d bytes sent in %g seconds (%g Kbytes/s)\n",
					bytecount, seconds, float(bytecount)/seconds/1000.0);

			close(fd);
			close(data_sd);

			readreply();
		}
		else if (strcmp(command, "delete") == 0) //------------ DELETE -------------
		{
			checkctl();
			char * file;
			file = strtok(NULL, " \t");
			if (file == NULL)
			{
				cout << "usage: delete filename" << endl;
				continue;
			}
			xarg("usage: delete filename");

			switch(fork()) // see faq q4
			{
				case 0: // child process
					exit(readreply());
				default: // parent process
					fprintf(out, "DELE %s\r\n", file);
					fflush(out);
			}
			int status;
			wait(&status);
		}
		else if (strcmp(command, "close") == 0) //------------- CLOSE --------------
		{
			xarg("usage: close");

			switch(fork()) // see faq q4
			{
			case 0:  // child process
				readreply();
				exit(0);
			default: // parent process
				fprintf(out, "QUIT\r\n");
				fflush(out);
			}
			int status;
			wait(&status);

			if (close(cmd_sd) == 0)
			{
				cmd_sd = -1;
			}
		}
		else if (strcmp(command, "quit") == 0) //-------------- QUIT ---------------
		{
			xarg("usage: quit");

			if (cmd_sd == -1)
			{
				break;
			}
			switch(fork()) // see faq q4
			{
			case 0:  // child process
				readreply();
				exit(0);
			default: // parent process
				fprintf(out, "QUIT\r\n");
				fflush(out);
			}
			int status;
			wait(&status);

			break;
		}
		else
		{
			cout << "unknown or unimplemented command" << endl;
		}
	}
}

