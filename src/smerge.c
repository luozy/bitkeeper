#include "system.h"
#include "sccs.h"

private int	do_merge(int start, int end, FILE *inputs[3]);
private void	usage(void);
private char	*fagets(FILE *fh);
private int	resolve_conflict(char **lines[3]);
private char	**unidiff(char **gca, char **new);
private void	show_examples(void);
private	int	parse_range(char *range, int *start, int *end);

typedef struct region region;
private int	merge_same_changes(region *r);
private int	merge_only_one(region *r);
private int	merge_content(region *r);
private int	merge_common_header(region *r);
private int	merge_common_footer(region *r);
private	int	merge_common_deletes(region *r);

private void	user_conflict(region *r);
private void	user_conflict_fdiff(region *r);


enum {
	MODE_GCA,
	MODE_2WAY,
	MODE_3WAY,
	MODE_NEWONLY
};
 
enum {
	LEFT,
	GCA, 
	RIGHT
};

#define VALID(lines, i) ((lines) && (lines)[i])

private	char	*revs[3];
private	char	*file;
private	int	automerge = 0;
private	int	mode = MODE_GCA;
private	int	show_seq;
private	int	do_merge_common = 1;
private	int	do_merge_content;
private	int	fdiff;
private	FILE	*fl, *fr;
private	char	*anno = 0;

int
smerge_main(int ac, char **av)
{
	int	c;
	int	i;
	char	buf[MAXPATH];
	FILE	*inputs[3];
	int	start = 0, end = 0;
	int	ret;

	while ((c = getopt(ac, av, "23A:aCcefhnr:s")) != -1) {
		switch (c) {
		    case '2': /* 2 way format (like diff3) */
			mode = MODE_2WAY;
			break;
		    case '3': /* 3 way format (shows gca) */
			mode = MODE_3WAY;
			break;
		    case 'n': /* newonly (like -2 except marks added lines) */
			mode = MODE_NEWONLY;
			break;
		    case 'A': /* Add annotations */
		    	anno = optarg;
			break;
		    case 'a': /* automerge non-overlapping changes */
			automerge = 1;
			break;
		    case 'f': /* fdiff output mode */
			fdiff = 1;
			break;
		    case 'C': /* do not collapse same changes on both sides */
			do_merge_common = 0;
			break;
		    case 'e': /* show examples */
			show_examples();
			return(2);
		    case 'r': /* show output in the range <r> */
			if (parse_range(optarg, &start, &end)) {
				usage();
				return (2);
			}
			break;
		    case 's': /* show sequence numbers */
			show_seq = 1;
			break;
		    case 'c': /* experimental content merge */
			do_merge_content = 1;
			break;
		    case 'h': /* help */
		    default:
			usage();
			return (2);
		}
	}
	if (ac - optind != 4) {
		usage();
		return (2);
	}
	if (mode == MODE_3WAY && fdiff) {
		fprintf(stderr, "3way mode is not legal with fdiff output\n");
		return (2);
	}
	file = av[optind + 3];
	for (i = 0; i < 3; i++) {
		revs[i] = av[optind + i];
		sprintf(buf, "bk get %s%s -qkpOr%s %s",
		    anno ? "-a" : "",
		    anno ? anno : "",
		    revs[i], file);
		inputs[i] = popen(buf, "r");
		assert(inputs[i]);
	}
	if (fdiff) {
		char *left = aprintf("/tmp/left.%d", getpid());
		char *right = aprintf("/tmp/right.%d", getpid());

		puts(left);
		puts(right);
		fl = fopen(left, "w");
		fr = fopen(right, "w");
		assert(fl && fr);
		free(left);
		free(right);
	}
	ret = do_merge(start, end, inputs);
	for (i = 0; i < 3; i++) {
		int	status;
		status = pclose(inputs[i]);
		if (status) {
			fprintf(stderr, 
				"Fetch of revision %s of file %s failed.\n",
				revs[i], file);
			ret = 2;
		}
	}
	if (fdiff) {
		fclose(fl);
		fclose(fr);
	}
	return (ret);
}

private void
usage(void)
{
	system("bk help -s smerge");
}

private char *
skipseq(char *p)
{
	if (anno) {
		++p;  /* skip first char */
		while (isspace(*p)) ++p;
		while (isdigit(*p)) ++p;
		assert(*p == ' ');
		++p;
	} else {
		p = strchr(p, '\t');
		assert(p);
		p++;
	}
	return (p);
}

private void
printline(char *line, int preserve_char)
{
	if (preserve_char) {
		assert(!fdiff);
		fputc(line[0], stdout);
		++line;
	}
	unless (show_seq) line = skipseq(line);
	if (fdiff) {
		fputc(' ', fl);
		fputs(line, fl);
		fputc(' ', fr);
		fputs(line, fr);
	} else {
		fputs(line, stdout);
	}
}
			
private int
do_merge(int start, int end, FILE *inputs[3])
{
	char	**lines[3];
	u32	seq[3];
	int	i;
	char	*curr[3];
	int	ret = 0;
	
#define NEWLINE(fh) (seq[i] = (curr[i] = fagets(fh)) ? atoi(curr[i]) : ~0)
	for (i = 0; i < 3; i++) {
		NEWLINE(inputs[i]);
		lines[i] = 0;
	}
	while (1) {
		u32	max = 0;
		for (i = 0; i < 3; i++) max = seq[i] > max ? seq[i] : max;
		
		for (i = 0; i < 3; i++) {
			while (seq[i] < max) {
				lines[i] = addLine(lines[i], curr[i]);
				NEWLINE(inputs[i]);
			}
		}
		if (seq[0] == max &&
		    seq[1] == max &&
		    seq[2] == max) {
			if (max > start) {
				if (lines[0] || lines[1] || lines[2]) {
					if (resolve_conflict(lines)) {
						ret = 1;
					}
					lines[0] = lines[1] = lines[2] = 0;
				}
				if (end && max >= end) break;
				if (max == ~0) break;
				printline(curr[0], 0);
			}
			for (i = 0; i < 3; i++) {
				if (lines[i]) freeLines(lines[i]);
				lines[i] = 0;
				if (curr[i]) free(curr[i]);
				NEWLINE(inputs[i]);
			}
		}
	}
#undef	NEWLINE
	return (ret);
}

private char *
fagets(FILE *fh)
{
	int	size;
	int	len;
	char	*ret;

	size = 128;
	ret = malloc(size);
	len = 0;

	/*
	 * Read a line from the file with NO limit on linesize
	 */
	while (1) {
		if (!fgets(ret + len, size - len, fh)) {
			if (len) return (ret);
			free(ret);
			return 0;
		}
		len += strlen(ret + len);
		if (len + 1 < size) break;
		size <<= 1;
#ifdef	HAVE_REALLOC
		ret = realloc(ret, size);
#else		
		{
			/* cheap realloc()  bah! */
			char	*new;
			new = malloc(size);
			strcpy(new, ret);
			free(ret);
			ret = new;
		}
#endif
	}
	return (ret);
}


/*
 * return true if lines are the same
 */
private int
sameLines(char **a, char **b)
{
	int	i;

	EACH(a) {
		char	*al, *bl;
		unless (VALID(b, i)) return (0);
		al = strchr(a[i], anno ? '|' : '\t');
		bl = strchr(b[i], anno ? '|' : '\t');
		assert(al && bl);
		unless (streq(al, bl)) return (0);
	}
	if (VALID(b, i)) return (0);
	return (1);
}

struct region {
	char	**left;
	char	**gca;
	char	**right;
	char	**merged;
	int	automerged;
	region	*next;
};
private region *regions = 0;

private region *
pop_region(void)
{
	if (regions) {
		region	*ret;

		ret = regions;
		regions = regions->next;
		return (ret);
	} else {
		return (0);
	}
}

private void
push_region(region *r)
{	
	r->next = regions;
	regions = r;
}

/*
 * Define of set of autoresolve function for processing
 * a conflict each function takes an array of 3 groups of lines
 * for the local, gca, and remote.  And looks at them and
 * possiblibly prints some output with printline() and then 
 * returns. 
 * Return values:
 *    0  printed nothing, made no changes
 *    1  made some change
 *
 *   The functions can make calls to push_region() to schedule some
 *   lines to be processed after the current section is finished.
 *   The functions are not allowed to modify or delete the lines that
 *   are passed in, but they are allowed to split them into multiple
 *   regions.
 */
private	int	always_true = 1;
struct mergefcns {
	int	(*fcn)(region *r);
	int	*flag;
} mergefcns[] = {
	{merge_same_changes,	&do_merge_common},
	{merge_only_one,	&always_true},
	{merge_content,		&do_merge_content},
	{merge_common_header,   &do_merge_content},
	{merge_common_footer,   &do_merge_content},
	{merge_common_deletes,	&do_merge_content},
};
#define	N_MERGEFCNS (sizeof(mergefcns)/sizeof(struct mergefcns))

private int
resolve_conflict(char **lines[3])
{
	int	i;
	region	*r;
	int	ret = 0;

	new(r);
	r->left = lines[LEFT];
	r->gca = lines[GCA];
	r->right = lines[RIGHT];
	push_region(r);
	while (regions) {
		r = pop_region();
		if (automerge) {
			int	changed;
			do {
				changed = 0;
				for (i = 0; i < N_MERGEFCNS; i++) {
					unless (*mergefcns[i].flag) continue;
					changed |= mergefcns[i].fcn(r);
					if (r->automerged) break;
				}
			} while (changed && !r->automerged);
		}
		if (r->automerged) {
			/* This region was automerged */
			if (fdiff) {
				user_conflict_fdiff(r);
			} else {
				EACH(r->merged) {
					printline(r->merged[i], 0);
				}
			}
			freeLines(r->merged);
		} else {
			/* found a conflict that needs to be resolved 
			 * by the user 
			 */
			ret = 1;
			if (fdiff) {
				user_conflict_fdiff(r);
			} else {
				user_conflict(r);
			}
		}
		freeLines(r->left);
		freeLines(r->gca);
		freeLines(r->right);
		free(r);
	}
	return (ret);
}

/*
 * All the lines on both sides are identical.
 */
private int
merge_same_changes(region *r)
{
	int	i;

	if (sameLines(r->left, r->right)) {
		EACH(r->left) {
			/* XXX add char to indicate 'both' */
			r->merged = addLine(r->merged, strdup(r->left[i]));
		}
		r->automerged = 1;
		return (1);
	}
	return (0);
}

/*
 * Only one side make changes
 */
private int
merge_only_one(region *r)
{
	int	i;
	if (sameLines(r->left, r->gca)) {
		EACH(r->right) {
			/* add state to indicate 'right' ? */
			r->merged = addLine(r->merged, strdup(r->right[i]));
		}
		r->automerged = 1;
		return (1);
	}
	if (sameLines(r->right, r->gca)) {
		EACH(r->left) {
			/* add state to indicate 'left' ? */
			r->merged = addLine(r->merged, strdup(r->left[i]));
		}
		r->automerged = 1;
		return (1);
	}
	return (0);
}

private void
user_conflict(region *r)
{
	int	i;
	char	**diffs;

	switch (mode) {
	    case MODE_GCA:
		unless (sameLines(r->left, r->gca)) {
			printf("<<< local %s %s vs %s\n", 
			       file, revs[GCA], revs[LEFT]);
			diffs = unidiff(r->gca, r->left);
			EACH(diffs) printline(diffs[i], 1);
			freeLines(diffs);
		}
		unless (sameLines(r->right, r->gca)) {
			printf("<<< remote %s %s vs %s\n", 
			       file, revs[GCA], revs[RIGHT]);
			diffs = unidiff(r->gca, r->right);
			EACH(diffs) printline(diffs[i], 1);
			freeLines(diffs);
		}
		printf(">>>\n");
		break;
	    case MODE_2WAY:
		printf("<<< local %s %s\n", file, revs[LEFT]);
		EACH(r->left) printline(r->left[i], 0);
		printf("<<< remote %s %s\n", file, revs[RIGHT]);
		EACH(r->right) printline(r->right[i], 0);
		printf(">>>\n");
		break;
	    case MODE_3WAY:
		printf("<<< gca %s %s\n", file, revs[GCA]);
		EACH(r->gca) printline(r->gca[i], 0);
		printf("<<< local %s %s\n", file, revs[LEFT]);
		EACH(r->left) printline(r->left[i], 0);
		printf("<<< remote %s %s\n", file, revs[RIGHT]);
		EACH(r->right) printline(r->right[i], 0);
		printf(">>>\n");
		break;
	    case MODE_NEWONLY:
		unless (sameLines(r->left, r->gca)) {
			printf("<<< local %s %s vs %s\n", 
			       file, revs[GCA], revs[LEFT]);
			diffs = unidiff(r->gca, r->left);
			EACH(diffs) {
				if (diffs[i][0] != '-') {
					printline(diffs[i], 1);
				}
			}
			freeLines(diffs);
		}
		unless (sameLines(r->right, r->gca)) {
			printf("<<< remote %s %s vs %s\n", 
			       file, revs[GCA], revs[RIGHT]);
			diffs = unidiff(r->gca, r->right);
			EACH(diffs) {
				if (diffs[i][0] != '-') {
					printline(diffs[i], 1);
				}
			}
			freeLines(diffs);
		}
		printf(">>>\n");
		break;
	}
}

private u32
seq(char *line)
{
	return (line ? atoi(line + 1) : 0);
}

private char *
popLine(char **lines)
{
	char	*ret;
	int	i;

	assert(lines && lines[1]);
	if (show_seq) {
		ret = lines[1];
	} else {
		char	*trail = skipseq(lines[1]);
		if (mode == MODE_2WAY) {
			ret = trail;
		} else {
			ret = malloc(strlen(lines[1]));
			ret[0] = lines[1][0];
			strcpy(ret + 1, trail);
			free(lines[1]);
		}
	}
	/* shift array down by one */
	i = 1;
	while (VALID(lines, i+1)) {
		lines[i] = lines[i + 1];
		++i;
	}
	lines[i] = 0;
	return(ret);
}
	

private void
user_conflict_fdiff(region *r)
{
	int	i;
	char	**left = 0, **right = 0;
	char	**diffs;
	u32	sl, sr;

	switch (mode) {
	    case MODE_GCA:
		left = unidiff(r->gca, r->left);
		right = unidiff(r->gca, r->right);
		break;
	    case MODE_2WAY:
		EACH(r->left) left = addLine(left, r->left[i]);
		EACH(r->right) right = addLine(right, r->right[i]);
		break;
	    case MODE_NEWONLY:
		diffs = unidiff(r->gca, r->left);
		EACH(diffs) {
			if (diffs[i][0] != '-') {
				left = addLine(left, diffs[i]);
			}
		}
		free(diffs);
		diffs = unidiff(r->gca, r->right);
		EACH(diffs) {
			if (diffs[i][0] != '-') {
				right = addLine(right, diffs[i]);
			}
		}
		free(diffs);
		break;
	}
	EACH(left) if (r->automerged && left[i][0] == '+') left[i][0] = 'a';
	EACH(right) if (r->automerged && right[i][0] == '+') right[i][0] = 'a';
	sl = left ? seq(left[1]) : 0;
	sr = right ? seq(right[1]) : 0;
	while (sl && sr) {
		while (sl < sr) {
			fputs(popLine(left), fl);
			fputs("s\n", fr);
			sl = seq(left[1]);
			unless (sl) goto done;
		}
 		while (sl > sr) {
			fputs("s\n", fl);
			fputs(popLine(right), fr);
			sr = seq(right[1]);
			unless(sr) goto done;
		}
		while (sl == sr) {
			fputs(popLine(left), fl);
			fputs(popLine(right), fr);
			sl = seq(left[1]);
			sr = seq(right[1]);
			unless (sl && sr) goto done;
		}
	}
 done:
	while (sl) {
		fputs(popLine(left), fl);
		fputs("s\n", fr);
		sl = seq(left[1]);
	}
	while (sr) {
		fputs("s\n", fl);
		fputs(popLine(right), fr);
		sr = seq(right[1]);
	}
	freeLines(left);
	freeLines(right);
}

private char **
unidiff(char **gca, char **new)
{
	char	gcafile[MAXPATH], newfile[MAXPATH];
	char	buf[128];
	char	*cmd;
	FILE	*f;
	int	i;
	int	line;
	char	**out = 0;
	int	gcaline = 1;

	gettemp(gcafile, "gca");
	f = fopen(gcafile, "w");
	EACH(gca) fputs(gca[i], f);
	fclose(f);

	/* 
	 * XXX we should be able to send this command to stdin of the command
	 * below.
	 */
	gettemp(newfile, "new");
	f = fopen(newfile, "w");
	EACH(new) fputs(new[i], f);
	fclose(f);
	
	cmd = aprintf("diff --rcs --minimal %s %s", gcafile, newfile);
	f = popen(cmd, "r");
	line = 0;
	while (fnext(buf, f)) {
		char	*n;
		int	l, cnt;
		assert(buf[0] == 'd' || buf[0] == 'a');
		l = strtol(buf + 1, &n, 10);
		cnt = strtol(n, 0, 10);
		if (buf[0] == 'd') {
			while (line < l - 1) {
				n = malloc(strlen(gca[gcaline])+2);
				n[0] = ' ';
				strcpy(n + 1, gca[gcaline]);
				++gcaline;
				out = addLine(out, n);
				++line;
			}
			while (cnt--) {
				n = malloc(strlen(gca[gcaline])+2);
				n[0] = '-';
				strcpy(n + 1, gca[gcaline]);
				++gcaline;
				out = addLine(out, n);
				++line;
			}
		} else if (buf[0] == 'a') {
			while (line < l) {
				n = malloc(strlen(gca[gcaline])+2);
				n[0] = ' ';
				strcpy(n + 1, gca[gcaline]);
				++gcaline;
				out = addLine(out, n);
				++line;
			}
			while (cnt--) {
				fnext(buf, f);
				n = malloc(strlen(buf) + 2);
				n[0] = '+';
				strcpy(n + 1, buf);
				out = addLine(out, n);
			}
		}
	}
	while (VALID(gca, gcaline)) {
		char	*n;
		n = malloc(strlen(gca[gcaline]) + 2);
		n[0] = ' ';
		strcpy(n + 1, gca[gcaline]);
		++gcaline;
		out = addLine(out, n);
	}
	pclose(f);
	unlink(gcafile);
	unlink(newfile);
	return (out);
}

private void
show_examples(void)
{
	fputs(
"Summary of bk smerge output formats\n\
 default\n\
    <<< local slib.c 1.642.1.6 vs 1.645\n\
    		  sc = sccs_init(file, INIT_NOCKSUM|INIT_SAVEPROJ, s->proj);\n\
    -             assert(sc->tree);\n\
    -             sccs_sdelta(sc, sc->tree, file);\n\
    +             assert(HASGRAPH(sc));\n\
    +             sccs_sdelta(sc, sccs_ino(sc), file);\n\
    <<< remote slib.c 1.642.1.6 vs 1.642.2.1\n\
    -             sc = sccs_init(file, INIT_NOCKSUM|INIT_SAVEPROJ, s->proj);\n\
    +             sc = sccs_init(file, INIT_NOCKSUM|INIT_SAVEPROJ, p);\n\
    		  assert(sc->tree);\n\
    		  sccs_sdelta(sc, sc->tree, file);\n\
    >>>\n\
\n\
 -2  (2 way format (like diff3))\n\
    <<< local slib.c 1.645\n\
    		  sc = sccs_init(file, INIT_NOCKSUM|INIT_SAVEPROJ, s->proj);\n\
    		  assert(HASGRAPH(sc));\n\
    		  sccs_sdelta(sc, sccs_ino(sc), file);\n\
    <<< remote slib.c 1.642.2.1\n\
    		  sc = sccs_init(file, INIT_NOCKSUM|INIT_SAVEPROJ, p);\n\
    		  assert(sc->tree);\n\
    		  sccs_sdelta(sc, sc->tree, file);\n\
    >>>\n\
\n\
 -3  (3 way format (shows gca)\n\
    <<< gca slib.c 1.642.1.6\n\
    		  sc = sccs_init(file, INIT_NOCKSUM|INIT_SAVEPROJ, s->proj);\n\
    		  assert(sc->tree);\n\
    		  sccs_sdelta(sc, sc->tree, file);\n\
    <<< local slib.c 1.645\n\
    		  sc = sccs_init(file, INIT_NOCKSUM|INIT_SAVEPROJ, s->proj);\n\
    		  assert(HASGRAPH(sc));\n\
    		  sccs_sdelta(sc, sccs_ino(sc), file);\n\
    <<< remote slib.c 1.642.2.1\n\
    		  sc = sccs_init(file, INIT_NOCKSUM|INIT_SAVEPROJ, p);\n\
    		  assert(sc->tree);\n\
    		  sccs_sdelta(sc, sc->tree, file);\n\
    >>>\n\
\n\
 -n  ( newonly (like -2 except marks added lines))\n\
    <<< local slib.c 1.642.1.6 vs 1.645\n\
    		  sc = sccs_init(file, INIT_NOCKSUM|INIT_SAVEPROJ, s->proj);\n\
    +             assert(HASGRAPH(sc));\n\
    +             sccs_sdelta(sc, sccs_ino(sc), file);\n\
    <<< remote slib.c 1.642.1.6 vs 1.642.2.1\n\
    +             sc = sccs_init(file, INIT_NOCKSUM|INIT_SAVEPROJ, p);\n\
    		  assert(sc->tree);\n\
    		  sccs_sdelta(sc, sc->tree, file);\n\
    >>>\n\
", stdout);
}

private	int
parse_range(char *range, int *start, int *end)
{
	if (isdigit(range[0])) {
		*start = strtol(range, &range, 10);
	}
	if (range[0] == 0 || range[0] != '.' || range[1] != '.') {
		return (-1);
	}
	range += 2;
	if (isdigit(range[0])) {
		*end = strtol(range, 0, 10);
	}
	return(0);
}

private char **
lines_modified(char **diff)
{
	char	**out = 0;
	int	i;
	int	saw_deletes = 0;
	int	saw_adds = 0;

	EACH (diff) {
		switch (diff[i][0]) {
		    case ' ': break;
		    case '-':
			if (saw_adds) goto bad;
			saw_deletes = 1;
			out = addLine(out, strdup(diff[i]));
			break;
		    case '+':
			unless (saw_deletes) goto bad;
			saw_adds = 1;
			break;
		}
	}
	return (out);
 bad:
	freeLines(out);
	return (0);
}

private int
are_unmodified(char **diff, char **lines)
{
	int	i;
	int	lcnt = 1;
	u32	s;

	s = seq(lines[lcnt]);
	EACH (diff) {
		if (s < seq(diff[i])) return (0);
		if (s == seq(diff[i])) {
			if (diff[i][0] != ' ') return (0);
			++lcnt;
			s = seq(lines[lcnt]);
			unless (s) return (1);
		}
	}
	return (0);
}
	
private int
merge_content(region *r)
{
	char	**left, **right;
	char	**modified;
	int	i;
	int	ret = 0;
	int	rline;
	int	ok;

	left = unidiff(r->gca, r->left);
	right = unidiff(r->gca, r->right);
	
	modified = lines_modified(left);
	unless (modified) goto bad;
	ok = are_unmodified(right, modified);
	freeLines(modified);
	unless (ok) goto bad;

	modified = lines_modified(right);
	unless (modified) goto bad;
	ok = are_unmodified(left, modified);
	freeLines(modified);
	unless (ok) goto bad;
	
	/* we are good to go, do merge */
	rline = 1;
	EACH(left) {
		while (VALID(right, rline) && 
		       seq(right[rline]) < seq(left[i])) {
			if (right[i][0] == '+') {
				/* XXX add char to indicate 'right' ? */
				r->merged = addLine(r->merged, 
						    strdup(right[rline] + 1));
			}
			++rline;
		}
		if (VALID(right, rline) && seq(right[rline]) == seq(left[i])) {
			/* deleted line, ignore */
			if (!((left[i][0] == '-' && right[rline][0] == ' ') ||
			      (left[i][0] == ' ' && right[rline][0] == '-'))) {
				printf("ERROR:\n\t%s\t%s", 
				       left[i], right[rline]);
				exit(2);
			}
			rline++;
		} else {
			assert(left[i][0] == '+');
			/* XXX add char to indicate 'left' ? */
			r->merged = addLine(r->merged, 
					    strdup(left[i] + 1));
		}
	}
	while (VALID(right, rline)) {
		if (right[rline][0] == '+') {
			/* XXX add char to indicate 'right' ? */
			r->merged = addLine(r->merged, 
					    strdup(right[rline] + 1));
		}
		++rline;
	}
	r->automerged = 1;
	ret = 1;
 bad:
	freeLines(left);
	freeLines(right);
	return (ret);
}

private int
merge_common_header(region *r)
{
	char	**a = r->left;
	char	**b = r->right;
	int	i;
	int	save_idx;
	int	last_seq;
	region	*newr;

	EACH(a) {
		char	*al, *bl;
		unless (VALID(b, i)) break;
		al = strchr(a[i], anno ? '|' : '\t');
		bl = strchr(b[i], anno ? '|' : '\t');
		assert(al && bl);
		unless (streq(al, bl)) break;
	}
	if (i == 1) return (0);
	save_idx = i;
	last_seq = seq(a[i-1]);

	/* save common header and move rest to a new region */
	new(newr);
	EACH (a) {
		if (i >= save_idx) {
			newr->left = addLine(newr->left, a[i]);
			a[i] = 0;
		}
	}
	EACH (b) {
		if (i >= save_idx) {
			newr->right = addLine(newr->right, b[i]);
			b[i] = 0;
		}
	}
	EACH (r->gca) {
		if (seq(r->gca[i]) > last_seq) {
			newr->gca = addLine(newr->gca, r->gca[i]);
			r->gca[i] = 0;
		}
	}
	push_region(newr);

	return (1);
}

private int
merge_common_footer(region *r)
{
	char	**a = r->left;
	char	**b = r->right;
	int	i;
	int	end_a, end_b;
	int	ca, cb;
	region	*newr;
	u32	last_seq;

	EACH (a);
	ca = end_a = i - 1;
	EACH (b);
	cb = end_b = i - 1;

	unless (ca > 1 && cb > 1) return (0);
	
	while (ca > 0 && cb > 0 && VALID(a, ca) && VALID(b, cb)) {
		char	*al, *bl;
		al = strchr(a[ca], anno ? '|' : '\t');
		bl = strchr(b[cb], anno ? '|' : '\t');
		assert(al && bl);
		unless (streq(al, bl)) break;
		--ca;
		--cb;
	}
	unless (ca < end_a && cb < end_b) return (0);

	/* push common lines to new region */
	new(newr);
	for (i = ca + 1; VALID(a, i); ++i) {
		newr->left = addLine(newr->left, a[i]);
		a[i] = 0;
	}
	for (i = cb + 1; VALID(b, i); ++i) {
		newr->right = addLine(newr->right, b[i]);
		b[i] = 0;
	}
	/* move lines from GCA that are after these lines to new region */
	last_seq = MIN(seq(newr->left[1]), seq(newr->right[1]));
	EACH(r->gca) {
		if (seq(r->gca[i]) >= last_seq) {
			newr->gca = addLine(newr->gca, r->gca[i]);
			r->gca[i] = 0;
		}
	}
	push_region(newr);

	return (1);
}
	
private int
merge_common_deletes(region *r)
{
	char	**left, **right;
	int	i, j;
	int	ret = 0;
	int	cnt;
	int	len;
	region	*newr;

	left = unidiff(r->gca, r->left);
	right = unidiff(r->gca, r->right);

#if 0	
	/* 
	 * remove matching deletes at beginning
	 */
	EACH (left) if (left[i][0] != '-') break;
	cnt = i - 1;
	EACH (right) if (right[i][0] != '-') break;
	cnt = min(cnt, i - 1);
	
	if (cnt > 0) {
		ret = 1;
		lines_trim_head(r->gca, cnt);
	}
#endif
	
	/*
	 * move matching deletes at end to new region
	 */
	EACH (left);
	len = i - 1;
	for (j = len; j >= 1; --j) {
		if (left[j][0] != '-') break;
	}
	cnt = len - j;
	EACH (right);
	len = i - 1;
	for (j = len; j >= 1; --j) {
		if (right[j][0] != '-') break;
	}
	cnt = MIN(cnt, len - j);

	if (cnt > 0) {
		EACH (r->gca);
		len = i - 1;
		if (cnt < len) {
			new(newr);
			while (cnt > 0) {
				--i;
				--cnt;
				newr->gca = addLine(newr->gca,
						    r->gca[i]);
				r->gca[i] = 0;
			}
			push_region(newr);
			ret = 1;
		}
	}

	freeLines(left);
	freeLines(right);
	return (ret);
}
