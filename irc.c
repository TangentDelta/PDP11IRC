# include   <stdio.h>
# include   <sys/types.h>
# include   <sys/socket.h>
# include   <netinet/in.h>
# include   <netdb.h>
# include   <arpa/inet.h>

#include <signal.h>
#include <sys/select.h>

#include <varargs.h>
#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>

int SocketFD;

static char bufin[4096];
int indexbufin = 0;
static char bufout[4096];
int indexbufout = 0;
static char channel[256];
static time_t trespond;
static FILE *srv;

static char *server_host = "irc.slashnet.org";	/*Server to connect to*/
static int server_port = 6667;
static char *nick = "TangyPDP-11";	/*Your nick*/

static void
bufcat(s)
char *s;
{
	while((indexbufout < 4095) && (*s)){
			bufout[indexbufout++] = *s++;
			bufout[indexbufout] = 0;
	}
}

static void
clearbuf()
{
	memset(bufout, 0, sizeof bufout);
	indexbufout = 0;
}

static void
sout(fmt, va_alist)
char *fmt;
va_dcl
{
	va_list ap;

	va_start(ap);
	vsprintf(bufout, fmt, ap);
	va_end(ap);
	fprintf(srv, "%s\r\n", bufout);
}

static char*
skip(s, c)
char *s;
char c;
{
	while(*s != c && *s != '\0')
			s++;
	if(*s != '\0')
			*s++ = '\0';
	return s;
}

static void
trim(s)
char *s;
{
	char *e;

	e = s + strlen(s) - 1;
	while(isspace(*e) && e > s)
			e--;
	*(e + 1) = '\0';
}

static char *
eat_isspace(s,r)
char *s;
int r;
{
	while(*s != '\0' && isspace(*s) == r)
			s++;
	return s;
}

#define strlcpy _strlcpy
static void
strlcpy(to, from, l)
char *to;
char *from;
int l;
{
	memccpy(to, from, '\0', l);
	to[l-1] = '\0';
}

static void
parsesrv(cmd)
char *cmd;
{
	char *usr, *par, *txt;
	static char timestr[18];
	time_t t;
	t = time(NULL);
	strftime(timestr, sizeof timestr, "%R", localtime(&t));

	usr = server_host;
	if(!cmd || !*cmd)
			return;
	if(cmd[0] == ':') {
			usr = cmd + 1;
			cmd = skip(usr, ' ');
			if(cmd[0] == '\0')
					return;
			skip(usr, '!');
	}
	skip(cmd, '\r');
	par = skip(cmd, ' ');
	txt = skip(par, ':');
	trim(par);
	if(!strcmp("PONG", cmd))
			return;
	if(!strcmp("PRIVMSG", cmd))
			fprintf(stdout,"%s: %s <%s> %s\n",par,timestr,usr,txt);
	else if(!strcmp("PING", cmd)){
			sout("PONG %s", txt);
	}
	else {
			fprintf(stdout, "%s: %s >< %s (%s): %s\n", usr, timestr, cmd, par, txt);
			if(!strcmp("NICK", cmd) && !strcmp(usr, nick))
					strlcpy(nick, txt, sizeof nick);
	}
}

static void
privmsg(channel,msg)
char *channel;
char *msg;
{
	static char timestr[18];
	time_t t;
	t = time(NULL);
	strftime(timestr, sizeof timestr, "%R", localtime(&t));

	if(channel[0] == '\0') {
			printf("No channel to send to\n");
			return;
	}
	fprintf(stdout,"%s: %s <%s> %s\n",channel,timestr,nick,msg);
	sout("PRIVMSG %s :%s", channel, msg);
	/*clearbuf();
	bufcat("PRIVMSG ");
	bufcat(channel);
	bufcat(" :");
	bufcat(msg);
	fprintf(srv, "%s\r\n", bufout);*/
}

static void
parsein(s)
char *s;
{
	char c, *p;

	if(s[0] == '\0')
			return;
	skip(s, '\n');
	if(s[0] != ':') {
			privmsg(channel, s);
			return;
	}
	c = *++s;
	if(c != '\0' && isspace(s[1])) {
			p = s + 2;
			switch(c) {
			case 'j':
					sout("JOIN %s", p);
					if(channel[0] == '\0')
							strlcpy(channel, p, sizeof channel);
					return;
			case 'l':
					s = eat_isspace(p, 1);
					p = eat_isspace(s, 0);
					if(!*s)
							s = channel;
					if(*p)
							*p++ = '\0';
					if(!*p)
							p = "sicPDP - IRC on a PDP-11";

					sout("PART %s :%s", s, p);
					return;
			case 'm':
					s = eat_isspace(p, 1);
					p = eat_isspace(s, 0);
					if(*p)
							*p++ = '\0';
					privmsg(s, p);
					return;
			case 's':
					strlcpy(channel, p, sizeof channel);
					return;
			}
	}
	sout("%s", s);
}

main(argc, argv)
char **argv;
{
	int i, c;
	struct timeval tv;
	char *user = getenv("USER");
	fd_set rd;

	i = dial(server_host, server_port);

	srv = fdopen(i, "r+");

	sout("NICK %s", nick);
	sout("USER %s localhost %s :%s", user, server_host, nick);

	fflush(srv);

	setbuf(stdout, NULL);
	setbuf(srv, NULL);

	for(;;) { /* main loop */
			FD_ZERO(&rd);
			FD_SET(0, &rd);
			FD_SET(fileno(srv), &rd);
			tv.tv_sec = 120;
			tv.tv_usec = 0;
			i = select(fileno(srv) + 1, &rd, 0, 0, &tv);
			if(i < 0) {
					if(errno == EINTR)
							continue;
					perror("sic: error on select():");
			}
			else if(i == 0) {
					if(time(NULL) - trespond >= 300)
							perror("sic shutting down: parse timeout\n");
					sout("PING %s", server_host);
					continue;
			}
			if(FD_ISSET(fileno(srv), &rd)) {
					if(fgets(bufin, sizeof bufin, srv) == NULL)
							perror("sic: remote host closed connection\n");
					parsesrv(bufin);
					trespond = time(NULL);
			}
			if(FD_ISSET(0, &rd)) {
					if(fgets(bufin, sizeof bufin, stdin) == NULL)
							perror("sic: broken pipe\n");
					parsein(bufin);
			}
	}

	shutdown(srv, 2);
	close(srv);
	shutdown(SocketFD, 2);
	close(SocketFD);
	return 0;
}

int dial(host, port)
char *host;
int port;
{
	register struct hostent *hp;
	struct sockaddr_in sa;
	int res;

	if ((hp = gethostbyname(host)) == NULL) {
			perror("Cannot resolve host");
	}



	SocketFD = socket(AF_INET, SOCK_STREAM, 0);
	if(SocketFD == -1){
			perror("Cannot create socket");
			exit(1);
	}

	memset(&sa, 0, sizeof sa);

	sa.sin_family = AF_INET;
	sa.sin_port = htons(6667);
	sa.sin_addr = *((struct in_addr *) hp->h_addr_list[0]);

	/*res = inet_pton(AF_INET, hp->h_addr_list[0], &sa.sin_addr);*/

	if (connect(SocketFD, (struct sockaddr *)&sa, sizeof sa) == -1) {
			perror("Connect failed");
			close(SocketFD);
			exit(1);
	}

	printf("Connected!\n");

	return SocketFD;
}