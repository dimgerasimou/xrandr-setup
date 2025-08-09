/*
 * See LICENSE file for copyright and license details.
 *
 * xrandr-setup is designed as a configurable XRandR helper for
 * multimonitor setups. Using a config file can load default
 * configurations for different setups using the monitor id,
 * with the main users in mind, laptop users with hybrid graphics
 * using one or more external monitors.
 *
 * The configuration file layout is explained in the README.md file.
 *
 * To understand everything else, start reading main().
 */

#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <libgen.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <X11/extensions/Xrandr.h>
#include <X11/Xlib.h>

#include "toml.h"

/* constants definition */
#define LOG_SIZE 256
#define BUF_SIZE 512

/* paths definitions */
const char *cfgpath[] = { "$XDG_CONFIG_HOME", "xrandr-setup", "xrandr-setup.toml", NULL};
const char *logpath[] = { "$HOME", "window-manager.log", NULL};
const char *pmtpath[] = { "usr", "local", "bin", "dmenu", NULL};

/* structure definitions */
typedef struct {
	RRMode rid;
	char *id;
	double rate;
	unsigned int primary;
	unsigned int xoffset;
	unsigned int yoffset;
	unsigned int xmode;
	unsigned int ymode;
	unsigned int moderot;
	Rotation rotation;
} CfgMonitor;

typedef struct {
	unsigned int dpi;
	unsigned int lowp;
	char *name;
	size_t mc;
	CfgMonitor **m;
} CfgScreen;

typedef struct {
	size_t sc;
	CfgScreen **s;
} CfgScreens;

/* function definitions */
static void cleanup(CfgScreens *cs);
static void dielog(const char *func);
static void freemonitor(CfgMonitor *m);
static void freescreen(CfgScreen *s);
static void freescreens(CfgScreens *cs);
static CfgScreens* getcfgscreens(void);
static FILE* getcfgstream(void);
static int getinputscreen(CfgScreens *cs, char *argv[]);
static char* getpath(const char **arr);
static int getpromptoption(const char *menu, char *argv[]);
static void logstring(const char *string);
static void matchscreens(CfgScreens *cs);
static void newmonitor(CfgScreen *s);
static void newscreen(CfgScreens *ss);
static void setupmonitor(CfgMonitor *m, XRROutputInfo *output);
static void parsescreen(CfgScreens *cs, TomlArray *screen);
static void printhelp(void);
static void removescreen(CfgScreens *cs, const int index);
static void setscreen(CfgScreen *s);
static void setup(void);
static void setupemptyscreen(CfgScreen **s);
static void setupmonitor(CfgMonitor *m, XRROutputInfo *output);
static void setupscreen(CfgScreen *s);
static void setupscreensize(CfgScreen *s, const unsigned int retract);

/* variable definitions */
static Display *dpy = NULL;
static XRRScreenResources *resources = NULL;
static Window root;
static unsigned int lowperf;

static void
cleanup(CfgScreens *cs)
{
	freescreens(cs);
	if (resources) {
		XRRFreeScreenResources(resources);
		resources = NULL;
	}
	if (dpy) {
		XCloseDisplay(dpy);
		dpy = NULL;
	}
}

static void
dielog(const char *func)
{
	char log[LOG_SIZE];

	sprintf(log, "ERROR - %s failed - %s", func, strerror(errno));
	logstring(log);
	exit(errno);
}

static void
freemonitor(CfgMonitor *m)
{
	if (!m)
		return;

	if (m->id)
		free(m->id);

	free(m);
	m = NULL;
}

static void
freescreen(CfgScreen *s)
{
	if (!s)
		return;

	for (size_t j = 0; j < s->mc; j++) {
		freemonitor(s->m[j]);
	}

	free(s->m);
	free(s->name);
	free(s);
	s = NULL;
}

static void
freescreens(CfgScreens *cs)
{
	if (!cs)
		return;

	for (size_t i = 0; i < cs->sc; i ++)
		freescreen(cs->s[i]);

	free(cs);
	cs = NULL;
}

static CfgScreens*
getcfgscreens(void)
{
	FILE *fp;
	CfgScreens *cs;
	TomlArray *config = NULL;
	TomlArrayKey *screens = NULL;

	if (!(fp = getcfgstream()))
		return NULL;
	
	config = tomlgetconfig(fp);
	fclose(fp);

	if (!config) {
		return NULL;
	}

	if (!(cs = malloc(sizeof(CfgScreens))))
		dielog("malloc()");

	cs->sc = 0;
	cs->s = NULL;

	if (!(screens = tomlgetarraykey(config, "screen"))) {
		tomldeletearray(config);
		return cs;
	}

	for (size_t i = 0; i < screens->narr; i++)
		parsescreen(cs, screens->arr[i]);

	tomldeletearray(config);
	return cs;
}

static FILE*
getcfgstream(void)
{
	FILE *fp;
	char *path;

	path = getpath(cfgpath);
	if (access(path, F_OK)) {
		char log[LOG_SIZE];

		sprintf(log, "WARN - File: %s does not exist", path);
		logstring(log);
		return NULL;
	}

	if (!(fp = fopen(path, "r"))) {
		char log[LOG_SIZE];

		sprintf(log, "ERROR - Failed to open file: %s - %s", path, strerror(errno));
		logstring(log);
		exit(errno);
	}

	return fp;
}

static int
getinputscreen(CfgScreens *cs, char *argv[])
{
	size_t pstrs;
	char *pstr;
	char buffer[BUF_SIZE];
	char ret;

	if (cs->sc < 1)
		return -1;

	if (!(pstr = malloc(sizeof(char)))) {
		cleanup(cs);
		dielog("malloc()");
	}

	pstr[0] = '\0';
	pstrs = 1;

	for (size_t i = 0; i < cs->sc; i++) {
		int ret = snprintf(buffer, sizeof(buffer), "%s\t%zu\n", cs->s[i]->name, i);
		
		if ((size_t) ret > sizeof(buffer) - 1 || ret < 0) {
			logstring("ERROR - snprintf() failed - buffer overflow (or encoding error)");
			free(pstr);
			cleanup(cs);
			exit(errno);
		}

		pstrs += strlen(buffer);

		if (!(pstr = realloc(pstr, pstrs * sizeof(char)))) {
			cleanup(cs);
			dielog("realloc()");
		}
		strcat(pstr, buffer);
	}
	if (pstr[pstrs - 2] == '\n')
		 pstr[pstrs - 2] = '\0';
	
	ret = getpromptoption(pstr, argv);
	free(pstr);

	if (ret < 0) {
		cleanup(cs);
		exit(0);
	}

	return ret;
}

static char*
getpath(const char **arr)
{
	
	char *path;
	char *env;
	size_t spath = 1;
	
	if (!(path = malloc(sizeof(char))))
		dielog("malloc()");
	path[0] = '\0';

	for (int i = 0; arr[i] != NULL; i++) {
		if (arr[i][0] == '$') {
			if ((env = getenv(arr[i] + 1))) {
				spath += strlen(env);
				if(!(path = realloc(path, spath * sizeof(char))))
					dielog("realloc()");
				strcat(path, env);
				continue;
			}

			if (!strcmp(arr[i] + 1, "XDG_CONFIG_HOME")) {
				if (!(env = getenv("HOME"))) {
					fprintf(stderr, "ERROR - Failed to get env variable: HOME - %s\n", strerror(errno));
					exit(errno);
				}
				spath += strlen(env) + strlen("/.config");
				if(!(path = realloc(path, spath * sizeof(char))))
					dielog("realloc()");
				strcat(path, env);
				strcat(path, "/.config");
				continue;
			}

			fprintf(stderr, "ERROR - Failed to get env variable: %s - %s\n", arr[i], strerror(errno));
			exit(errno);
		} else {
			spath += strlen(arr[i]) + 1;
			if(!(path = realloc(path, spath * sizeof(char))))
				dielog("realloc()");
			strcat(path, "/");
			strcat(path, arr[i]);
		}
	}

	return path;
}

/* spawns the prompt application and returns the index of the selected screen */
static int
getpromptoption(const char *menu, char *argv[])
{
	int option;
	int writepipe[2];
	int readpipe[2];
	char buffer[BUF_SIZE];
	char *path;
	char **args;
	char *name = NULL;
	char *ptr;
	size_t argc = 0;

	path = getpath(pmtpath);
	for (char **ptr = (char**) &pmtpath; *ptr != NULL; ptr++) 
		name = *ptr;

	for (char **ptr = argv; *ptr != NULL; ptr++)
		argc++;

	args = malloc((argc + 2) * sizeof (char*));
	if (!args)
		dielog("malloc()");

	args[0] = strdup(name);
	for (size_t i = 0; i < argc; i++) {
		args[i+1] = strdup(argv[i]);
	}
	args[argc+1] = NULL;

	option = -EREMOTEIO;
	buffer[0] = '\0';

	if (pipe(writepipe) < 0 || pipe(readpipe) < 0) {
		logstring("Failed to initialize pipes");
		return -ESTRPIPE;
	}
	
	switch (fork()) {
		case -1:
			logstring("fork() failed");
			return -ECHILD;

		case 0: /* child - prompt application */
			close(writepipe[1]);
			close(readpipe[0]);

			dup2(writepipe[0], STDIN_FILENO);
			close(writepipe[0]);

			dup2(readpipe[1], STDOUT_FILENO);
			close(readpipe[1]);
			
			execv(path, args);
			exit(EXIT_FAILURE);

		default: /* parent */
			close(writepipe[0]);
			close(readpipe[1]);

			write(writepipe[1], menu, strlen(menu) + 1);
			close(writepipe[1]);

			wait(NULL);

			read(readpipe[0], buffer, sizeof(buffer));
			close(readpipe[0]);
	}
	ptr = strchr(buffer, '\t');
	if (ptr != NULL)
		sscanf(ptr, "%d", &option);

	for (char **ptr = args; *ptr != NULL; ptr++)
		if (*ptr)
			free(*ptr);
	free(args);
	free(path);

	return option;
}

static void
logstring(const char *string)
{
	if (!string)
		return;

	FILE *fp;
	char *path;
	time_t rawtime;
	struct tm *timeinfo;

	path = getpath(logpath);

	if (!(fp = fopen(path, "a"))) {
		fprintf(stderr, "ERROR - Failed to open in append mode, path: %s - %s\n", path, strerror(errno));
		exit(errno);
	}

	time(&rawtime);
	timeinfo = localtime(&rawtime);

	fprintf(fp, "%d-%02d-%02d %02d:%02d:%02d xrandr-setup\n%s\n\n", timeinfo->tm_year+1900,
		timeinfo->tm_mon+1, timeinfo->tm_mday, timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec, string);
	
	fclose(fp);
	free(path);
}

/* removes all screens that don't match the connected monitors */
static void
matchscreens(CfgScreens *cs)
{
	XRROutputInfo *output;
	size_t mc;
	char **id;
	char **temp;
	unsigned int match;

	if (!cs)
		return;

	if (!(id = malloc(sizeof(char*))))
		dielog("malloc()");
	mc = 0;

	for (int i = 0; i < resources->noutput; i++) {
		output = XRRGetOutputInfo(dpy, resources, resources->outputs[i]);
		if (!output->connection) {
			mc++;
			if (!(temp = realloc(id, mc * sizeof(char*))))
				dielog("realloc()");
			temp[mc-1] = strdup(output->name);
			id = temp;
		}
		XRRFreeOutputInfo(output);
	}

	for (size_t i = cs->sc - 1; i > 0; i--) {
		if (cs->s[i]->mc != mc) {
			removescreen(cs, i);
			continue;
		}
		for (size_t j = 0; j < mc; j++) {
			match = 0;
			for (size_t k = 0; k < mc; k++) {
				if (!strcmp(cs->s[i]->m[j]->id, id[k]))
					match++;
			}
			if (match != 1) {
				removescreen(cs, i);
				break;
			}
		}
	}
}

static void
newmonitor(CfgScreen *s)
{
	CfgMonitor *m;

	s->mc++;
	if (!(s->m = realloc(s->m, s->mc * sizeof(CfgMonitor*))))
		dielog("realloc()");

	if (!(m = malloc(sizeof(CfgMonitor))))
		dielog("malloc()");

	s->m[s->mc - 1] = m;

	m->id       = NULL;
	m->primary  = 0;
	m->xoffset  = 0;
	m->yoffset  = 0;
	m->xmode    = 0;
	m->ymode    = 0;
	m->rate     = 0.0;
	m->moderot  = 0;
	m->rotation = RR_Rotate_0;
}

static void
newscreen(CfgScreens *ss)
{
	CfgScreen *s;

	ss->sc++;
	if (!(ss->s = realloc(ss->s, ss->sc * sizeof(CfgScreen*))))
		dielog("realloc()");

	if (!(s = malloc(sizeof(CfgScreen))))
		dielog("malloc()");

	ss->s[ss->sc - 1] = s;
	s->dpi = 0;
	s->lowp = 0;
	s->mc = 0;
	s->m = NULL;
	s->name = NULL;
}

static void
parsemonitor(CfgScreen *s, TomlArray *monitor)
{
	char *id;
	unsigned int primary;
	unsigned int xoffset;
	unsigned int yoffset;
	unsigned int xmode;
	unsigned int ymode;
	double rate;
	char *rotation;

	newmonitor(s);

	if (!tomlgetstring(monitor, "id", &id))
		s->m[s->mc - 1]->id = strdup(id);
	if (!tomlgetbool(monitor, "primary", &primary))
		s->m[s->mc - 1]->primary = primary;
	if (!tomlgetuint(monitor, "xoffset", &xoffset))
		s->m[s->mc - 1]->xoffset = xoffset;
	if (!tomlgetuint(monitor, "yoffset", &yoffset))
		s->m[s->mc - 1]->yoffset = yoffset;
	if (!tomlgetuint(monitor, "xmode", &xmode))
		s->m[s->mc - 1]->xmode = xmode;
	if (!tomlgetuint(monitor, "ymode", &ymode))
		s->m[s->mc - 1]->ymode = ymode;
	if (!tomlgetdouble(monitor, "rate", &rate))
		s->m[s->mc - 1]->rate = rate;
	if (!tomlgetstring(monitor, "rotation", &rotation)) {
		if (!strcmp(rotation, "normal"))
			s->m[s->mc - 1]->rotation = RR_Rotate_0;
		else if (!strcmp(rotation, "inverted"))
			s->m[s->mc - 1]->rotation = RR_Rotate_180;
		else if (!strcmp(rotation, "left"))
			s->m[s->mc - 1]->rotation = RR_Rotate_270;
		else if (!strcmp(rotation, "right"))
			s->m[s->mc - 1]->rotation = RR_Rotate_90;
	}
}

static void
parsescreen(CfgScreens *cs, TomlArray *screen)
{
	TomlArrayKey *monitors = NULL;
	char *name;
	unsigned int dpi;
	unsigned int lowp;

	newscreen(cs);

	if (!tomlgetstring(screen, "name", &name))
		cs->s[cs->sc - 1]->name = strdup(name);
	if (!tomlgetuint(screen, "dpi", &dpi))
		cs->s[cs->sc - 1]->dpi = dpi;
	if (!tomlgetbool(screen, "low-performance", &lowp))
		cs->s[cs->sc - 1]->lowp = lowp;

	if (!(monitors = tomlgetarraykey(screen, "monitor")))
		return;

	for (size_t i = 0; i < monitors->narr; i++)
		parsemonitor(cs->s[cs->sc - 1], monitors->arr[i]);
}

static void printhelp(void)
{
    printf("xrandr-setup - Configure screen layouts using xRandR with predefined configs\n\n");

    printf("Usage:\n");
    printf("  xrandr-setup [OPTION]...\n\n");

    printf("Options:\n");
    printf("  -h, --help             Display this help and exit\n");
    printf("  -a, --auto             Set up a basic screen layout ignoring config files\n");
    printf("  -s, --select [ARGS]    Prompt for layout selection (passes additional arguments)\n");
    printf("  -l, --low-performance  Enable low performance mode (60 Hz rate cap)\n\n");
}

static void
removescreen(CfgScreens *cs, const int index)
{
	freescreen(cs->s[index]);
	for (size_t i = index; i < cs->sc - 1; i++)
		cs->s[i] = cs->s[i + 1];
	cs->sc--;
}

static void
setscreen(CfgScreen *s)
{
	XRROutputInfo *output;
	XRRCrtcInfo *crtc;

	for (size_t i = 0; i < s->mc; i++) {
		if (s->m[i]->rid == 0) {
			setupemptyscreen(&s);
			logstring("WARN - Configuration error. Loading default config.");
			break;
		}
	}

	setupscreensize(s, 0);

	for (int i = 0; i < resources->noutput; i++) {
		output = XRRGetOutputInfo(dpy, resources, resources->outputs[i]);
		for (size_t j = 0; j < s->mc; j++) {
			if (!strcmp(s->m[j]->id, output->name)) {
				crtc = XRRGetCrtcInfo(dpy, resources, output->crtc);
				XRRSetCrtcConfig(dpy, resources, output->crtc,
				                 crtc->timestamp, s->m[j]->xoffset, s->m[j]->yoffset,
				                 s->m[j]->rid, s->m[j]->rotation, crtc->outputs,
				                 crtc->noutput);
				XRRFreeCrtcInfo(crtc);
				if (s->m[j]->primary)
					XRRSetOutputPrimary(dpy, root, resources->outputs[i]);
			}
		}
		XRRFreeOutputInfo(output);
	}

	setupscreensize(s, 1);
}

static void
setup(void)
{
	dpy = XOpenDisplay(NULL);
	
	if (dpy == NULL)
		dielog("XOpenDisplay()");

	root = XDefaultRootWindow(dpy);
	resources = XRRGetScreenResources(dpy, root);
	lowperf = 0;
}

static void
setupemptyscreen(CfgScreen **s)
{
	XRROutputInfo *output;

	if (*s) {
		freescreen(*s);
		*s = NULL;
	}

	*s = malloc(sizeof(CfgScreen));
	(*s)->mc = 0;
	(*s)->m = NULL;
	(*s)->dpi = 0;
	(*s)->name = NULL;

	for (int i = 0; i < resources->noutput; i++) {
		output = XRRGetOutputInfo(dpy, resources, resources->outputs[i]);
		if (!output->connection) {
			newmonitor(*s);
			(*s)->m[(*s)->mc - 1]->id = strdup(output->name);
		}
		XRRFreeOutputInfo(output);
	}
}

static void
setupmonitor(CfgMonitor *m, XRROutputInfo *output)
{
	XRRModeInfo *mode;
	double rate;
	char str1[16];
	char str2[16];

	if (!m->xmode) {
		for (int i = 0; i < output->nmode; i++) {
			for (int j = 0; j < resources->nmode; j++) {
				if (output->modes[i] == resources->modes[j].id) {
					mode = &resources->modes[j];
					if (mode->width > m->xmode)
						m->xmode = mode->width;
				}
			}
		}
	}

	if (!m->ymode) {
		for (int i = 0; i < output->nmode; i++) {
			for (int j = 0; j < resources->nmode; j++) {
				if (output->modes[i] == resources->modes[j].id) {
					mode = &resources->modes[j];
					if (mode->width != m->xmode)
						continue;
					if (mode->height > m->ymode)
						m->ymode = mode->height;
				}
			}
		}
	}

	if (m->rate == 0.0) {
		for (int i = 0; i < output->nmode; i++) {
			for (int j = 0; j < resources->nmode; j++) {
				if (output->modes[i] == resources->modes[j].id) {
					mode = &resources->modes[j];
					if ((mode->width != m->xmode) || (mode->height!= m->ymode))
						continue;
					rate = (double) mode->dotClock / (double) (mode->hTotal * mode->vTotal);
					if (rate > m->rate && (rate <= 60.0 || !lowperf))
						m->rate = rate;
					
				}
			}
		}
	}

	/* check if the final monitor mode is valid */
	for (int i = 0; i < output->nmode; i++) {
		for (int j = 0; j < resources->nmode; j++) {
			if (output->modes[i] == resources->modes[j].id) {
				mode = &resources->modes[j];
				if ((mode->width != m->xmode) || (mode->height!= m->ymode))
					continue;
				sprintf(str1, "%.0lf", m->rate);
				sprintf(str2, "%.0lf", (double) mode->dotClock / (double) (mode->hTotal * mode->vTotal));
				if (!strcmp(str1, str2)) {
					m->rid = mode->id;
					return;
				}
			}
		}
	}
}

/* fills all the missing data of the selected screen */
static void
setupscreen(CfgScreen *s)
{
	XRROutputInfo *output;

	for (int i = 0; i < resources->noutput; i++) {
		output = XRRGetOutputInfo(dpy, resources, resources->outputs[i]);
		if (!output->connection) {
			for (size_t i = 0; i < s->mc; i++) {
				if (!strcmp(s->m[i]->id, output->name)) {
					setupmonitor(s->m[i], output);
					break;
				}
			}
		}
		XRRFreeOutputInfo(output);
	}
}

static void
setupscreensize(CfgScreen *s, unsigned int retract)
{
	XRRScreenSize *scr;
	unsigned int temp;
	unsigned int width = 0;
	unsigned int height = 0;
	unsigned int mmWidth;
	unsigned int mmHeight;
	int nsizes;
	double dpi;
	
	for (size_t i = 0; i < s->mc ; i++) {
		if ((s->m[i]->rotation == RR_Rotate_90 || s->m[i]->rotation == RR_Rotate_270) && !s->m[i]->moderot) {
			temp = s->m[i]->xmode;
			s->m[i]->xmode = s->m[i]->ymode;
			s->m[i]->ymode= temp;
			s->m[i]->moderot++;
		}
	}

	for (size_t i = 0; i < s->mc ; i++) {
		if (s->m[i]->yoffset + s->m[i]->ymode > height)
			height = s->m[i]->yoffset + s->m[i]->ymode;
		if (s->m[i]->xoffset + s->m[i]->xmode > width)
			width = s->m[i]->xoffset + s->m[i]->xmode;
	}

	scr = XRRSizes(dpy, 0, &nsizes);

	if (s->dpi)
		dpi = (double) s->dpi;
	else
		dpi = (25.4 * scr[0].height) / scr[0].mheight;
	
	mmWidth  = (int) ((25.4 * width) / dpi);
	mmHeight = (int) ((25.4 * height) / dpi);

	if (!retract) {
		if (scr[0].width > (int) width) {
			width = scr[0].width;
			mmWidth = scr[0].mwidth;
		}
		
		if (scr[0].height > (int) height) {
			height = scr[0].height;
			mmHeight = scr[0].mheight;
		}
	}
	XRRSetScreenSize(dpy, root, width, height, mmWidth, mmHeight);
}

int
main(int argc, char *argv[])
{
	CfgScreens *cs;
	CfgScreen *s;
	unsigned int selscreen = 0;
	unsigned int autoselect = 0;

	setup();
	cs = getcfgscreens();
	matchscreens(cs);

	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--auto") || !strcmp(argv[i], "-a")) {
			autoselect = 1;
		} else if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h")) {
			printhelp();
			return 0;
		} else if (!strcmp(argv[i], "--select") || !strcmp(argv[i], "-s")) {
			selscreen = getinputscreen(cs, &argv[i+1]);
		} else if (!strcmp(argv[i], "--low-performance") || !strcmp(argv[i], "-l")) {
			lowperf = 1;
			matchscreens(cs);
		} else {
			fprintf(stderr, "Usage: %s [-ahls]\n", argv[0]);
			return 1;
		}
	}

	

	if (cs && !autoselect && cs->sc > 0) {
		s = cs->s[selscreen];
	} else {
		if (cs) {
			freescreens(cs);
			cs = NULL;
		}
		cs = malloc(sizeof(CfgScreens));
		cs->sc = 0;
		cs->s = NULL;

		newscreen(cs);
		s = cs->s[0];
		setupemptyscreen(&s);
	}
	
	setupscreen(s);
	setscreen(s);
	cleanup(cs);
	return 0;
}
