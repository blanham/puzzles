/*
 * solo.c: the number-placing puzzle most popularly known as `Sudoku'.
 *
 * TODO:
 *
 *  - reports from users are that `Trivial'-mode puzzles are still
 *    rather hard compared to newspapers' easy ones, so some better
 *    low-end difficulty grading would be nice
 *     + it's possible that really easy puzzles always have
 *       _several_ things you can do, so don't make you hunt too
 *       hard for the one deduction you can currently make
 *     + it's also possible that easy puzzles require fewer
 *       cross-eliminations: perhaps there's a higher incidence of
 *       things you can deduce by looking only at (say) rows,
 *       rather than things you have to check both rows and columns
 *       for
 *     + but really, what I need to do is find some really easy
 *       puzzles and _play_ them, to see what's actually easy about
 *       them
 *     + while I'm revamping this area, filling in the _last_
 *       number in a nearly-full row or column should certainly be
 *       permitted even at the lowest difficulty level.
 *     + also Owen noticed that `Basic' grids requiring numeric
 *       elimination are actually very hard, so I wonder if a
 *       difficulty gradation between that and positional-
 *       elimination-only might be in order
 *     + but it's not good to have _too_ many difficulty levels, or
 *       it'll take too long to randomly generate a given level.
 * 
 *  - it might still be nice to do some prioritisation on the
 *    removal of numbers from the grid
 *     + one possibility is to try to minimise the maximum number
 * 	 of filled squares in any block, which in particular ought
 * 	 to enforce never leaving a completely filled block in the
 * 	 puzzle as presented.
 *
 *  - alternative interface modes
 *     + sudoku.com's Windows program has a palette of possible
 * 	 entries; you select a palette entry first and then click
 * 	 on the square you want it to go in, thus enabling
 * 	 mouse-only play. Useful for PDAs! I don't think it's
 * 	 actually incompatible with the current highlight-then-type
 * 	 approach: you _either_ highlight a palette entry and then
 * 	 click, _or_ you highlight a square and then type. At most
 * 	 one thing is ever highlighted at a time, so there's no way
 * 	 to confuse the two.
 *     + then again, I don't actually like sudoku.com's interface;
 *       it's too much like a paint package whereas I prefer to
 *       think of Solo as a text editor.
 *     + another PDA-friendly possibility is a drag interface:
 *       _drag_ numbers from the palette into the grid squares.
 *       Thought experiments suggest I'd prefer that to the
 *       sudoku.com approach, but I haven't actually tried it.
 */

/*
 * Solo puzzles need to be square overall (since each row and each
 * column must contain one of every digit), but they need not be
 * subdivided the same way internally. I am going to adopt a
 * convention whereby I _always_ refer to `r' as the number of rows
 * of _big_ divisions, and `c' as the number of columns of _big_
 * divisions. Thus, a 2c by 3r puzzle looks something like this:
 *
 *   4 5 1 | 2 6 3
 *   6 3 2 | 5 4 1
 *   ------+------     (Of course, you can't subdivide it the other way
 *   1 4 5 | 6 3 2     or you'll get clashes; observe that the 4 in the
 *   3 2 6 | 4 1 5     top left would conflict with the 4 in the second
 *   ------+------     box down on the left-hand side.)
 *   5 1 4 | 3 2 6
 *   2 6 3 | 1 5 4
 *
 * The need for a strong naming convention should now be clear:
 * each small box is two rows of digits by three columns, while the
 * overall puzzle has three rows of small boxes by two columns. So
 * I will (hopefully) consistently use `r' to denote the number of
 * rows _of small boxes_ (here 3), which is also the number of
 * columns of digits in each small box; and `c' vice versa (here
 * 2).
 *
 * I'm also going to choose arbitrarily to list c first wherever
 * possible: the above is a 2x3 puzzle, not a 3x2 one.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <ctype.h>
#include <math.h>

#ifdef STANDALONE_SOLVER
#include <stdarg.h>
int solver_show_working, solver_recurse_depth;
#endif

#include "puzzles.h"

/*
 * To save space, I store digits internally as unsigned char. This
 * imposes a hard limit of 255 on the order of the puzzle. Since
 * even a 5x5 takes unacceptably long to generate, I don't see this
 * as a serious limitation unless something _really_ impressive
 * happens in computing technology; but here's a typedef anyway for
 * general good practice.
 */
typedef unsigned char digit;
#define ORDER_MAX 255

#define PREFERRED_TILE_SIZE 32
#define TILE_SIZE (ds->tilesize)
#define BORDER (TILE_SIZE / 2)

#define FLASH_TIME 0.4F

enum { SYMM_NONE, SYMM_ROT2, SYMM_ROT4, SYMM_REF2, SYMM_REF2D, SYMM_REF4,
       SYMM_REF4D, SYMM_REF8 };

enum { DIFF_BLOCK, DIFF_SIMPLE, DIFF_INTERSECT,
       DIFF_SET, DIFF_RECURSIVE, DIFF_AMBIGUOUS, DIFF_IMPOSSIBLE };

enum {
    COL_BACKGROUND,
    COL_GRID,
    COL_CLUE,
    COL_USER,
    COL_HIGHLIGHT,
    COL_ERROR,
    COL_PENCIL,
    NCOLOURS
};

struct game_params {
    int c, r, symm, diff;
};

struct game_state {
    int c, r;
    digit *grid;
    unsigned char *pencil;             /* c*r*c*r elements */
    unsigned char *immutable;	       /* marks which digits are clues */
    int completed, cheated;
};

static game_params *default_params(void)
{
    game_params *ret = snew(game_params);

    ret->c = ret->r = 3;
    ret->symm = SYMM_ROT2;	       /* a plausible default */
    ret->diff = DIFF_BLOCK;	       /* so is this */

    return ret;
}

static void free_params(game_params *params)
{
    sfree(params);
}

static game_params *dup_params(game_params *params)
{
    game_params *ret = snew(game_params);
    *ret = *params;		       /* structure copy */
    return ret;
}

static int game_fetch_preset(int i, char **name, game_params **params)
{
    static struct {
        char *title;
        game_params params;
    } presets[] = {
        { "2x2 Trivial", { 2, 2, SYMM_ROT2, DIFF_BLOCK } },
        { "2x3 Basic", { 2, 3, SYMM_ROT2, DIFF_SIMPLE } },
        { "3x3 Trivial", { 3, 3, SYMM_ROT2, DIFF_BLOCK } },
        { "3x3 Basic", { 3, 3, SYMM_ROT2, DIFF_SIMPLE } },
        { "3x3 Intermediate", { 3, 3, SYMM_ROT2, DIFF_INTERSECT } },
        { "3x3 Advanced", { 3, 3, SYMM_ROT2, DIFF_SET } },
        { "3x3 Unreasonable", { 3, 3, SYMM_ROT2, DIFF_RECURSIVE } },
#ifndef SLOW_SYSTEM
        { "3x4 Basic", { 3, 4, SYMM_ROT2, DIFF_SIMPLE } },
        { "4x4 Basic", { 4, 4, SYMM_ROT2, DIFF_SIMPLE } },
#endif
    };

    if (i < 0 || i >= lenof(presets))
        return FALSE;

    *name = dupstr(presets[i].title);
    *params = dup_params(&presets[i].params);

    return TRUE;
}

static void decode_params(game_params *ret, char const *string)
{
    ret->c = ret->r = atoi(string);
    while (*string && isdigit((unsigned char)*string)) string++;
    if (*string == 'x') {
        string++;
        ret->r = atoi(string);
	while (*string && isdigit((unsigned char)*string)) string++;
    }
    while (*string) {
        if (*string == 'r' || *string == 'm' || *string == 'a') {
            int sn, sc, sd;
            sc = *string++;
            if (*string == 'd') {
                sd = TRUE;
                string++;
            } else {
                sd = FALSE;
            }
            sn = atoi(string);
            while (*string && isdigit((unsigned char)*string)) string++;
            if (sc == 'm' && sn == 8)
                ret->symm = SYMM_REF8;
            if (sc == 'm' && sn == 4)
                ret->symm = sd ? SYMM_REF4D : SYMM_REF4;
            if (sc == 'm' && sn == 2)
                ret->symm = sd ? SYMM_REF2D : SYMM_REF2;
            if (sc == 'r' && sn == 4)
                ret->symm = SYMM_ROT4;
            if (sc == 'r' && sn == 2)
                ret->symm = SYMM_ROT2;
            if (sc == 'a')
                ret->symm = SYMM_NONE;
        } else if (*string == 'd') {
            string++;
            if (*string == 't')        /* trivial */
                string++, ret->diff = DIFF_BLOCK;
            else if (*string == 'b')   /* basic */
                string++, ret->diff = DIFF_SIMPLE;
            else if (*string == 'i')   /* intermediate */
                string++, ret->diff = DIFF_INTERSECT;
            else if (*string == 'a')   /* advanced */
                string++, ret->diff = DIFF_SET;
            else if (*string == 'u')   /* unreasonable */
                string++, ret->diff = DIFF_RECURSIVE;
        } else
            string++;                  /* eat unknown character */
    }
}

static char *encode_params(game_params *params, int full)
{
    char str[80];

    sprintf(str, "%dx%d", params->c, params->r);
    if (full) {
        switch (params->symm) {
          case SYMM_REF8: strcat(str, "m8"); break;
          case SYMM_REF4: strcat(str, "m4"); break;
          case SYMM_REF4D: strcat(str, "md4"); break;
          case SYMM_REF2: strcat(str, "m2"); break;
          case SYMM_REF2D: strcat(str, "md2"); break;
          case SYMM_ROT4: strcat(str, "r4"); break;
          /* case SYMM_ROT2: strcat(str, "r2"); break; [default] */
          case SYMM_NONE: strcat(str, "a"); break;
        }
        switch (params->diff) {
          /* case DIFF_BLOCK: strcat(str, "dt"); break; [default] */
          case DIFF_SIMPLE: strcat(str, "db"); break;
          case DIFF_INTERSECT: strcat(str, "di"); break;
          case DIFF_SET: strcat(str, "da"); break;
          case DIFF_RECURSIVE: strcat(str, "du"); break;
        }
    }
    return dupstr(str);
}

static config_item *game_configure(game_params *params)
{
    config_item *ret;
    char buf[80];

    ret = snewn(5, config_item);

    ret[0].name = "Columns of sub-blocks";
    ret[0].type = C_STRING;
    sprintf(buf, "%d", params->c);
    ret[0].sval = dupstr(buf);
    ret[0].ival = 0;

    ret[1].name = "Rows of sub-blocks";
    ret[1].type = C_STRING;
    sprintf(buf, "%d", params->r);
    ret[1].sval = dupstr(buf);
    ret[1].ival = 0;

    ret[2].name = "Symmetry";
    ret[2].type = C_CHOICES;
    ret[2].sval = ":None:2-way rotation:4-way rotation:2-way mirror:"
        "2-way diagonal mirror:4-way mirror:4-way diagonal mirror:"
        "8-way mirror";
    ret[2].ival = params->symm;

    ret[3].name = "Difficulty";
    ret[3].type = C_CHOICES;
    ret[3].sval = ":Trivial:Basic:Intermediate:Advanced:Unreasonable";
    ret[3].ival = params->diff;

    ret[4].name = NULL;
    ret[4].type = C_END;
    ret[4].sval = NULL;
    ret[4].ival = 0;

    return ret;
}

static game_params *custom_params(config_item *cfg)
{
    game_params *ret = snew(game_params);

    ret->c = atoi(cfg[0].sval);
    ret->r = atoi(cfg[1].sval);
    ret->symm = cfg[2].ival;
    ret->diff = cfg[3].ival;

    return ret;
}

static char *validate_params(game_params *params, int full)
{
    if (params->c < 2 || params->r < 2)
	return "Both dimensions must be at least 2";
    if (params->c > ORDER_MAX || params->r > ORDER_MAX)
	return "Dimensions greater than "STR(ORDER_MAX)" are not supported";
    return NULL;
}

/* ----------------------------------------------------------------------
 * Solver.
 * 
 * This solver is used for several purposes:
 *  + to generate filled grids as the basis for new puzzles (by
 *    supplying no clue squares at all)
 *  + to check solubility of a grid as we gradually remove numbers
 *    from it
 *  + to solve an externally generated puzzle when the user selects
 *    `Solve'.
 * 
 * It supports a variety of specific modes of reasoning. By
 * enabling or disabling subsets of these modes we can arrange a
 * range of difficulty levels.
 */

/*
 * Modes of reasoning currently supported:
 *
 *  - Positional elimination: a number must go in a particular
 *    square because all the other empty squares in a given
 *    row/col/blk are ruled out.
 *
 *  - Numeric elimination: a square must have a particular number
 *    in because all the other numbers that could go in it are
 *    ruled out.
 *
 *  - Intersectional analysis: given two domains which overlap
 *    (hence one must be a block, and the other can be a row or
 *    col), if the possible locations for a particular number in
 *    one of the domains can be narrowed down to the overlap, then
 *    that number can be ruled out everywhere but the overlap in
 *    the other domain too.
 *
 *  - Set elimination: if there is a subset of the empty squares
 *    within a domain such that the union of the possible numbers
 *    in that subset has the same size as the subset itself, then
 *    those numbers can be ruled out everywhere else in the domain.
 *    (For example, if there are five empty squares and the
 *    possible numbers in each are 12, 23, 13, 134 and 1345, then
 *    the first three empty squares form such a subset: the numbers
 *    1, 2 and 3 _must_ be in those three squares in some
 *    permutation, and hence we can deduce none of them can be in
 *    the fourth or fifth squares.)
 *     + You can also see this the other way round, concentrating
 *       on numbers rather than squares: if there is a subset of
 *       the unplaced numbers within a domain such that the union
 *       of all their possible positions has the same size as the
 *       subset itself, then all other numbers can be ruled out for
 *       those positions. However, it turns out that this is
 *       exactly equivalent to the first formulation at all times:
 *       there is a 1-1 correspondence between suitable subsets of
 *       the unplaced numbers and suitable subsets of the unfilled
 *       places, found by taking the _complement_ of the union of
 *       the numbers' possible positions (or the spaces' possible
 *       contents).
 * 
 *  - Recursion. If all else fails, we pick one of the currently
 *    most constrained empty squares and take a random guess at its
 *    contents, then continue solving on that basis and see if we
 *    get any further.
 */

/*
 * Within this solver, I'm going to transform all y-coordinates by
 * inverting the significance of the block number and the position
 * within the block. That is, we will start with the top row of
 * each block in order, then the second row of each block in order,
 * etc.
 * 
 * This transformation has the enormous advantage that it means
 * every row, column _and_ block is described by an arithmetic
 * progression of coordinates within the cubic array, so that I can
 * use the same very simple function to do blockwise, row-wise and
 * column-wise elimination.
 */
#define YTRANS(y) (((y)%c)*r+(y)/c)
#define YUNTRANS(y) (((y)%r)*c+(y)/r)

struct solver_usage {
    int c, r, cr;
    /*
     * We set up a cubic array, indexed by x, y and digit; each
     * element of this array is TRUE or FALSE according to whether
     * or not that digit _could_ in principle go in that position.
     *
     * The way to index this array is cube[(x*cr+y)*cr+n-1].
     * y-coordinates in here are transformed.
     */
    unsigned char *cube;
    /*
     * This is the grid in which we write down our final
     * deductions. y-coordinates in here are _not_ transformed.
     */
    digit *grid;
    /*
     * Now we keep track, at a slightly higher level, of what we
     * have yet to work out, to prevent doing the same deduction
     * many times.
     */
    /* row[y*cr+n-1] TRUE if digit n has been placed in row y */
    unsigned char *row;
    /* col[x*cr+n-1] TRUE if digit n has been placed in row x */
    unsigned char *col;
    /* blk[(y*c+x)*cr+n-1] TRUE if digit n has been placed in block (x,y) */
    unsigned char *blk;
};
#define cubepos(x,y,n) (((x)*usage->cr+(y))*usage->cr+(n)-1)
#define cube(x,y,n) (usage->cube[cubepos(x,y,n)])

/*
 * Function called when we are certain that a particular square has
 * a particular number in it. The y-coordinate passed in here is
 * transformed.
 */
static void solver_place(struct solver_usage *usage, int x, int y, int n)
{
    int c = usage->c, r = usage->r, cr = usage->cr;
    int i, j, bx, by;

    assert(cube(x,y,n));

    /*
     * Rule out all other numbers in this square.
     */
    for (i = 1; i <= cr; i++)
	if (i != n)
	    cube(x,y,i) = FALSE;

    /*
     * Rule out this number in all other positions in the row.
     */
    for (i = 0; i < cr; i++)
	if (i != y)
	    cube(x,i,n) = FALSE;

    /*
     * Rule out this number in all other positions in the column.
     */
    for (i = 0; i < cr; i++)
	if (i != x)
	    cube(i,y,n) = FALSE;

    /*
     * Rule out this number in all other positions in the block.
     */
    bx = (x/r)*r;
    by = y % r;
    for (i = 0; i < r; i++)
	for (j = 0; j < c; j++)
	    if (bx+i != x || by+j*r != y)
		cube(bx+i,by+j*r,n) = FALSE;

    /*
     * Enter the number in the result grid.
     */
    usage->grid[YUNTRANS(y)*cr+x] = n;

    /*
     * Cross out this number from the list of numbers left to place
     * in its row, its column and its block.
     */
    usage->row[y*cr+n-1] = usage->col[x*cr+n-1] =
	usage->blk[((y%r)*c+(x/r))*cr+n-1] = TRUE;
}

static int solver_elim(struct solver_usage *usage, int start, int step
#ifdef STANDALONE_SOLVER
                       , char *fmt, ...
#endif
                       )
{
    int c = usage->c, r = usage->r, cr = c*r;
    int fpos, m, i;

    /*
     * Count the number of set bits within this section of the
     * cube.
     */
    m = 0;
    fpos = -1;
    for (i = 0; i < cr; i++)
	if (usage->cube[start+i*step]) {
	    fpos = start+i*step;
	    m++;
	}

    if (m == 1) {
	int x, y, n;
	assert(fpos >= 0);

	n = 1 + fpos % cr;
	y = fpos / cr;
	x = y / cr;
	y %= cr;

        if (!usage->grid[YUNTRANS(y)*cr+x]) {
#ifdef STANDALONE_SOLVER
            if (solver_show_working) {
                va_list ap;
		printf("%*s", solver_recurse_depth*4, "");
                va_start(ap, fmt);
                vprintf(fmt, ap);
                va_end(ap);
                printf(":\n%*s  placing %d at (%d,%d)\n",
                       solver_recurse_depth*4, "", n, 1+x, 1+YUNTRANS(y));
            }
#endif
            solver_place(usage, x, y, n);
            return +1;
        }
    } else if (m == 0) {
#ifdef STANDALONE_SOLVER
	if (solver_show_working) {
	    va_list ap;
	    printf("%*s", solver_recurse_depth*4, "");
	    va_start(ap, fmt);
	    vprintf(fmt, ap);
	    va_end(ap);
	    printf(":\n%*s  no possibilities available\n",
		   solver_recurse_depth*4, "");
	}
#endif
        return -1;
    }

    return 0;
}

static int solver_intersect(struct solver_usage *usage,
                            int start1, int step1, int start2, int step2
#ifdef STANDALONE_SOLVER
                            , char *fmt, ...
#endif
                            )
{
    int c = usage->c, r = usage->r, cr = c*r;
    int ret, i;

    /*
     * Loop over the first domain and see if there's any set bit
     * not also in the second.
     */
    for (i = 0; i < cr; i++) {
        int p = start1+i*step1;
        if (usage->cube[p] &&
            !(p >= start2 && p < start2+cr*step2 &&
              (p - start2) % step2 == 0))
            return 0;		       /* there is, so we can't deduce */
    }

    /*
     * We have determined that all set bits in the first domain are
     * within its overlap with the second. So loop over the second
     * domain and remove all set bits that aren't also in that
     * overlap; return +1 iff we actually _did_ anything.
     */
    ret = 0;
    for (i = 0; i < cr; i++) {
        int p = start2+i*step2;
        if (usage->cube[p] &&
            !(p >= start1 && p < start1+cr*step1 && (p - start1) % step1 == 0))
        {
#ifdef STANDALONE_SOLVER
            if (solver_show_working) {
                int px, py, pn;

                if (!ret) {
                    va_list ap;
		    printf("%*s", solver_recurse_depth*4, "");
                    va_start(ap, fmt);
                    vprintf(fmt, ap);
                    va_end(ap);
                    printf(":\n");
                }

                pn = 1 + p % cr;
                py = p / cr;
                px = py / cr;
                py %= cr;

                printf("%*s  ruling out %d at (%d,%d)\n",
                       solver_recurse_depth*4, "", pn, 1+px, 1+YUNTRANS(py));
            }
#endif
            ret = +1;		       /* we did something */
            usage->cube[p] = 0;
        }
    }

    return ret;
}

struct solver_scratch {
    unsigned char *grid, *rowidx, *colidx, *set;
};

static int solver_set(struct solver_usage *usage,
                      struct solver_scratch *scratch,
                      int start, int step1, int step2
#ifdef STANDALONE_SOLVER
                      , char *fmt, ...
#endif
                      )
{
    int c = usage->c, r = usage->r, cr = c*r;
    int i, j, n, count;
    unsigned char *grid = scratch->grid;
    unsigned char *rowidx = scratch->rowidx;
    unsigned char *colidx = scratch->colidx;
    unsigned char *set = scratch->set;

    /*
     * We are passed a cr-by-cr matrix of booleans. Our first job
     * is to winnow it by finding any definite placements - i.e.
     * any row with a solitary 1 - and discarding that row and the
     * column containing the 1.
     */
    memset(rowidx, TRUE, cr);
    memset(colidx, TRUE, cr);
    for (i = 0; i < cr; i++) {
        int count = 0, first = -1;
        for (j = 0; j < cr; j++)
            if (usage->cube[start+i*step1+j*step2])
                first = j, count++;

	/*
	 * If count == 0, then there's a row with no 1s at all and
	 * the puzzle is internally inconsistent. However, we ought
	 * to have caught this already during the simpler reasoning
	 * methods, so we can safely fail an assertion if we reach
	 * this point here.
	 */
	assert(count > 0);
        if (count == 1)
            rowidx[i] = colidx[first] = FALSE;
    }

    /*
     * Convert each of rowidx/colidx from a list of 0s and 1s to a
     * list of the indices of the 1s.
     */
    for (i = j = 0; i < cr; i++)
        if (rowidx[i])
            rowidx[j++] = i;
    n = j;
    for (i = j = 0; i < cr; i++)
        if (colidx[i])
            colidx[j++] = i;
    assert(n == j);

    /*
     * And create the smaller matrix.
     */
    for (i = 0; i < n; i++)
        for (j = 0; j < n; j++)
            grid[i*cr+j] = usage->cube[start+rowidx[i]*step1+colidx[j]*step2];

    /*
     * Having done that, we now have a matrix in which every row
     * has at least two 1s in. Now we search to see if we can find
     * a rectangle of zeroes (in the set-theoretic sense of
     * `rectangle', i.e. a subset of rows crossed with a subset of
     * columns) whose width and height add up to n.
     */

    memset(set, 0, n);
    count = 0;
    while (1) {
        /*
         * We have a candidate set. If its size is <=1 or >=n-1
         * then we move on immediately.
         */
        if (count > 1 && count < n-1) {
            /*
             * The number of rows we need is n-count. See if we can
             * find that many rows which each have a zero in all
             * the positions listed in `set'.
             */
            int rows = 0;
            for (i = 0; i < n; i++) {
                int ok = TRUE;
                for (j = 0; j < n; j++)
                    if (set[j] && grid[i*cr+j]) {
                        ok = FALSE;
                        break;
                    }
                if (ok)
                    rows++;
            }

            /*
             * We expect never to be able to get _more_ than
             * n-count suitable rows: this would imply that (for
             * example) there are four numbers which between them
             * have at most three possible positions, and hence it
             * indicates a faulty deduction before this point or
             * even a bogus clue.
             */
            if (rows > n - count) {
#ifdef STANDALONE_SOLVER
		if (solver_show_working) {
		    va_list ap;
		    printf("%*s", solver_recurse_depth*4,
			   "");
		    va_start(ap, fmt);
		    vprintf(fmt, ap);
		    va_end(ap);
		    printf(":\n%*s  contradiction reached\n",
			   solver_recurse_depth*4, "");
		}
#endif
		return -1;
	    }

            if (rows >= n - count) {
                int progress = FALSE;

                /*
                 * We've got one! Now, for each row which _doesn't_
                 * satisfy the criterion, eliminate all its set
                 * bits in the positions _not_ listed in `set'.
                 * Return +1 (meaning progress has been made) if we
                 * successfully eliminated anything at all.
                 * 
                 * This involves referring back through
                 * rowidx/colidx in order to work out which actual
                 * positions in the cube to meddle with.
                 */
                for (i = 0; i < n; i++) {
                    int ok = TRUE;
                    for (j = 0; j < n; j++)
                        if (set[j] && grid[i*cr+j]) {
                            ok = FALSE;
                            break;
                        }
                    if (!ok) {
                        for (j = 0; j < n; j++)
                            if (!set[j] && grid[i*cr+j]) {
                                int fpos = (start+rowidx[i]*step1+
                                            colidx[j]*step2);
#ifdef STANDALONE_SOLVER
                                if (solver_show_working) {
                                    int px, py, pn;

                                    if (!progress) {
                                        va_list ap;
					printf("%*s", solver_recurse_depth*4,
					       "");
                                        va_start(ap, fmt);
                                        vprintf(fmt, ap);
                                        va_end(ap);
                                        printf(":\n");
                                    }

                                    pn = 1 + fpos % cr;
                                    py = fpos / cr;
                                    px = py / cr;
                                    py %= cr;

                                    printf("%*s  ruling out %d at (%d,%d)\n",
					   solver_recurse_depth*4, "",
                                           pn, 1+px, 1+YUNTRANS(py));
                                }
#endif
                                progress = TRUE;
                                usage->cube[fpos] = FALSE;
                            }
                    }
                }

                if (progress) {
                    return +1;
                }
            }
        }

        /*
         * Binary increment: change the rightmost 0 to a 1, and
         * change all 1s to the right of it to 0s.
         */
        i = n;
        while (i > 0 && set[i-1])
            set[--i] = 0, count--;
        if (i > 0)
            set[--i] = 1, count++;
        else
            break;                     /* done */
    }

    return 0;
}

static struct solver_scratch *solver_new_scratch(struct solver_usage *usage)
{
    struct solver_scratch *scratch = snew(struct solver_scratch);
    int cr = usage->cr;
    scratch->grid = snewn(cr*cr, unsigned char);
    scratch->rowidx = snewn(cr, unsigned char);
    scratch->colidx = snewn(cr, unsigned char);
    scratch->set = snewn(cr, unsigned char);
    return scratch;
}

static void solver_free_scratch(struct solver_scratch *scratch)
{
    sfree(scratch->set);
    sfree(scratch->colidx);
    sfree(scratch->rowidx);
    sfree(scratch->grid);
    sfree(scratch);
}

static int solver(int c, int r, digit *grid, random_state *rs, int maxdiff)
{
    struct solver_usage *usage;
    struct solver_scratch *scratch;
    int cr = c*r;
    int x, y, n, ret;
    int diff = DIFF_BLOCK;

    /*
     * Set up a usage structure as a clean slate (everything
     * possible).
     */
    usage = snew(struct solver_usage);
    usage->c = c;
    usage->r = r;
    usage->cr = cr;
    usage->cube = snewn(cr*cr*cr, unsigned char);
    usage->grid = grid;		       /* write straight back to the input */
    memset(usage->cube, TRUE, cr*cr*cr);

    usage->row = snewn(cr * cr, unsigned char);
    usage->col = snewn(cr * cr, unsigned char);
    usage->blk = snewn(cr * cr, unsigned char);
    memset(usage->row, FALSE, cr * cr);
    memset(usage->col, FALSE, cr * cr);
    memset(usage->blk, FALSE, cr * cr);

    scratch = solver_new_scratch(usage);

    /*
     * Place all the clue numbers we are given.
     */
    for (x = 0; x < cr; x++)
	for (y = 0; y < cr; y++)
	    if (grid[y*cr+x])
		solver_place(usage, x, YTRANS(y), grid[y*cr+x]);

    /*
     * Now loop over the grid repeatedly trying all permitted modes
     * of reasoning. The loop terminates if we complete an
     * iteration without making any progress; we then return
     * failure or success depending on whether the grid is full or
     * not.
     */
    while (1) {
        /*
         * I'd like to write `continue;' inside each of the
         * following loops, so that the solver returns here after
         * making some progress. However, I can't specify that I
         * want to continue an outer loop rather than the innermost
         * one, so I'm apologetically resorting to a goto.
         */
        cont:

	/*
	 * Blockwise positional elimination.
	 */
	for (x = 0; x < cr; x += r)
	    for (y = 0; y < r; y++)
		for (n = 1; n <= cr; n++)
		    if (!usage->blk[(y*c+(x/r))*cr+n-1]) {
			ret = solver_elim(usage, cubepos(x,y,n), r*cr
#ifdef STANDALONE_SOLVER
					  , "positional elimination,"
					  " %d in block (%d,%d)", n, 1+x/r, 1+y
#endif
					  );
			if (ret < 0) {
			    diff = DIFF_IMPOSSIBLE;
			    goto got_result;
			} else if (ret > 0) {
			    diff = max(diff, DIFF_BLOCK);
			    goto cont;
			}
                    }

	if (maxdiff <= DIFF_BLOCK)
	    break;

	/*
	 * Row-wise positional elimination.
	 */
	for (y = 0; y < cr; y++)
	    for (n = 1; n <= cr; n++)
		if (!usage->row[y*cr+n-1]) {
		    ret = solver_elim(usage, cubepos(0,y,n), cr*cr
#ifdef STANDALONE_SOLVER
				      , "positional elimination,"
				      " %d in row %d", n, 1+YUNTRANS(y)
#endif
				      );
		    if (ret < 0) {
			diff = DIFF_IMPOSSIBLE;
			goto got_result;
		    } else if (ret > 0) {
			diff = max(diff, DIFF_SIMPLE);
			goto cont;
		    }
                }
	/*
	 * Column-wise positional elimination.
	 */
	for (x = 0; x < cr; x++)
	    for (n = 1; n <= cr; n++)
		if (!usage->col[x*cr+n-1]) {
		    ret = solver_elim(usage, cubepos(x,0,n), cr
#ifdef STANDALONE_SOLVER
				      , "positional elimination,"
				      " %d in column %d", n, 1+x
#endif
				      );
		    if (ret < 0) {
			diff = DIFF_IMPOSSIBLE;
			goto got_result;
		    } else if (ret > 0) {
			diff = max(diff, DIFF_SIMPLE);
			goto cont;
		    }
                }

	/*
	 * Numeric elimination.
	 */
	for (x = 0; x < cr; x++)
	    for (y = 0; y < cr; y++)
		if (!usage->grid[YUNTRANS(y)*cr+x]) {
		    ret = solver_elim(usage, cubepos(x,y,1), 1
#ifdef STANDALONE_SOLVER
				      , "numeric elimination at (%d,%d)", 1+x,
				      1+YUNTRANS(y)
#endif
				      );
		    if (ret < 0) {
			diff = DIFF_IMPOSSIBLE;
			goto got_result;
		    } else if (ret > 0) {
			diff = max(diff, DIFF_SIMPLE);
			goto cont;
		    }
                }

	if (maxdiff <= DIFF_SIMPLE)
	    break;

        /*
         * Intersectional analysis, rows vs blocks.
         */
        for (y = 0; y < cr; y++)
            for (x = 0; x < cr; x += r)
                for (n = 1; n <= cr; n++)
		    /*
		     * solver_intersect() never returns -1.
		     */
                    if (!usage->row[y*cr+n-1] &&
                        !usage->blk[((y%r)*c+(x/r))*cr+n-1] &&
                        (solver_intersect(usage, cubepos(0,y,n), cr*cr,
                                          cubepos(x,y%r,n), r*cr
#ifdef STANDALONE_SOLVER
                                          , "intersectional analysis,"
                                          " %d in row %d vs block (%d,%d)",
                                          n, 1+YUNTRANS(y), 1+x/r, 1+y%r
#endif
                                          ) ||
                         solver_intersect(usage, cubepos(x,y%r,n), r*cr,
                                          cubepos(0,y,n), cr*cr
#ifdef STANDALONE_SOLVER
                                          , "intersectional analysis,"
                                          " %d in block (%d,%d) vs row %d",
                                          n, 1+x/r, 1+y%r, 1+YUNTRANS(y)
#endif
                                          ))) {
                        diff = max(diff, DIFF_INTERSECT);
                        goto cont;
                    }

        /*
         * Intersectional analysis, columns vs blocks.
         */
        for (x = 0; x < cr; x++)
            for (y = 0; y < r; y++)
                for (n = 1; n <= cr; n++)
                    if (!usage->col[x*cr+n-1] &&
                        !usage->blk[(y*c+(x/r))*cr+n-1] &&
                        (solver_intersect(usage, cubepos(x,0,n), cr,
                                          cubepos((x/r)*r,y,n), r*cr
#ifdef STANDALONE_SOLVER
                                          , "intersectional analysis,"
                                          " %d in column %d vs block (%d,%d)",
                                          n, 1+x, 1+x/r, 1+y
#endif
                                          ) ||
                         solver_intersect(usage, cubepos((x/r)*r,y,n), r*cr,
                                          cubepos(x,0,n), cr
#ifdef STANDALONE_SOLVER
                                          , "intersectional analysis,"
                                          " %d in block (%d,%d) vs column %d",
                                          n, 1+x/r, 1+y, 1+x
#endif
                                          ))) {
                        diff = max(diff, DIFF_INTERSECT);
                        goto cont;
                    }

	if (maxdiff <= DIFF_INTERSECT)
	    break;

	/*
	 * Blockwise set elimination.
	 */
	for (x = 0; x < cr; x += r)
	    for (y = 0; y < r; y++) {
		ret = solver_set(usage, scratch, cubepos(x,y,1), r*cr, 1
#ifdef STANDALONE_SOLVER
				 , "set elimination, block (%d,%d)", 1+x/r, 1+y
#endif
				 );
		if (ret < 0) {
		    diff = DIFF_IMPOSSIBLE;
		    goto got_result;
		} else if (ret > 0) {
		    diff = max(diff, DIFF_SET);
		    goto cont;
		}
	    }

	/*
	 * Row-wise set elimination.
	 */
	for (y = 0; y < cr; y++) {
            ret = solver_set(usage, scratch, cubepos(0,y,1), cr*cr, 1
#ifdef STANDALONE_SOLVER
			     , "set elimination, row %d", 1+YUNTRANS(y)
#endif
			     );
	    if (ret < 0) {
		diff = DIFF_IMPOSSIBLE;
		goto got_result;
	    } else if (ret > 0) {
		diff = max(diff, DIFF_SET);
		goto cont;
	    }
	}

	/*
	 * Column-wise set elimination.
	 */
	for (x = 0; x < cr; x++) {
            ret = solver_set(usage, scratch, cubepos(x,0,1), cr, 1
#ifdef STANDALONE_SOLVER
			     , "set elimination, column %d", 1+x
#endif
			     );
	    if (ret < 0) {
		diff = DIFF_IMPOSSIBLE;
		goto got_result;
	    } else if (ret > 0) {
		diff = max(diff, DIFF_SET);
		goto cont;
	    }
	}

	/*
	 * If we reach here, we have made no deductions in this
	 * iteration, so the algorithm terminates.
	 */
	break;
    }

    /*
     * Last chance: if we haven't fully solved the puzzle yet, try
     * recursing based on guesses for a particular square. We pick
     * one of the most constrained empty squares we can find, which
     * has the effect of pruning the search tree as much as
     * possible.
     */
    if (maxdiff >= DIFF_RECURSIVE) {
	int best, bestcount, bestnumber;

	best = -1;
	bestcount = cr+1;
	bestnumber = 0;

	for (y = 0; y < cr; y++)
	    for (x = 0; x < cr; x++)
		if (!grid[y*cr+x]) {
		    int count;

		    /*
		     * An unfilled square. Count the number of
		     * possible digits in it.
		     */
		    count = 0;
		    for (n = 1; n <= cr; n++)
			if (cube(x,YTRANS(y),n))
			    count++;

		    /*
		     * We should have found any impossibilities
		     * already, so this can safely be an assert.
		     */
		    assert(count > 1);

		    if (count < bestcount) {
			bestcount = count;
			bestnumber = 0;
		    }

		    if (count == bestcount) {
			bestnumber++;
			if (bestnumber == 1 ||
			    (rs && random_upto(rs, bestnumber) == 0))
			    best = y*cr+x;
		    }
		}

	if (best != -1) {
	    int i, j;
	    digit *list, *ingrid, *outgrid;

	    diff = DIFF_IMPOSSIBLE;    /* no solution found yet */

	    /*
	     * Attempt recursion.
	     */
	    y = best / cr;
	    x = best % cr;

	    list = snewn(cr, digit);
	    ingrid = snewn(cr * cr, digit);
	    outgrid = snewn(cr * cr, digit);
	    memcpy(ingrid, grid, cr * cr);

	    /* Make a list of the possible digits. */
	    for (j = 0, n = 1; n <= cr; n++)
		if (cube(x,YTRANS(y),n))
		    list[j++] = n;

#ifdef STANDALONE_SOLVER
	    if (solver_show_working) {
		char *sep = "";
		printf("%*srecursing on (%d,%d) [",
		       solver_recurse_depth*4, "", x, y);
		for (i = 0; i < j; i++) {
		    printf("%s%d", sep, list[i]);
		    sep = " or ";
		}
		printf("]\n");
	    }
#endif

	    /* Now shuffle the list. */
	    if (rs) {
		for (i = j; i > 1; i--) {
		    int p = random_upto(rs, i);
		    if (p != i-1) {
			int t = list[p];
			list[p] = list[i-1];
			list[i-1] = t;
		    }
		}
	    }

	    /*
	     * And step along the list, recursing back into the
	     * main solver at every stage.
	     */
	    for (i = 0; i < j; i++) {
		int ret;

		memcpy(outgrid, ingrid, cr * cr);
		outgrid[y*cr+x] = list[i];

#ifdef STANDALONE_SOLVER
		if (solver_show_working)
		    printf("%*sguessing %d at (%d,%d)\n",
			   solver_recurse_depth*4, "", list[i], x, y);
		solver_recurse_depth++;
#endif

		ret = solver(c, r, outgrid, rs, maxdiff);

#ifdef STANDALONE_SOLVER
		solver_recurse_depth--;
		if (solver_show_working) {
		    printf("%*sretracting %d at (%d,%d)\n",
			   solver_recurse_depth*4, "", list[i], x, y);
		}
#endif

		/*
		 * If we have our first solution, copy it into the
		 * grid we will return.
		 */
		if (diff == DIFF_IMPOSSIBLE && ret != DIFF_IMPOSSIBLE)
		    memcpy(grid, outgrid, cr*cr);

		if (ret == DIFF_AMBIGUOUS)
		    diff = DIFF_AMBIGUOUS;
		else if (ret == DIFF_IMPOSSIBLE)
		    /* do not change our return value */;
		else {
		    /* the recursion turned up exactly one solution */
		    if (diff == DIFF_IMPOSSIBLE)
			diff = DIFF_RECURSIVE;
		    else
			diff = DIFF_AMBIGUOUS;
		}

		/*
		 * As soon as we've found more than one solution,
		 * give up immediately.
		 */
		if (diff == DIFF_AMBIGUOUS)
		    break;
	    }

	    sfree(outgrid);
	    sfree(ingrid);
	    sfree(list);
	}

    } else {
        /*
         * We're forbidden to use recursion, so we just see whether
         * our grid is fully solved, and return DIFF_IMPOSSIBLE
         * otherwise.
         */
	for (y = 0; y < cr; y++)
	    for (x = 0; x < cr; x++)
		if (!grid[y*cr+x])
                    diff = DIFF_IMPOSSIBLE;
    }

    got_result:;

#ifdef STANDALONE_SOLVER
    if (solver_show_working)
	printf("%*s%s found\n",
	       solver_recurse_depth*4, "",
	       diff == DIFF_IMPOSSIBLE ? "no solution" :
	       diff == DIFF_AMBIGUOUS ? "multiple solutions" :
	       "one solution");
#endif

    sfree(usage->cube);
    sfree(usage->row);
    sfree(usage->col);
    sfree(usage->blk);
    sfree(usage);

    solver_free_scratch(scratch);

    return diff;
}

/* ----------------------------------------------------------------------
 * End of solver code.
 */

/* ----------------------------------------------------------------------
 * Solo filled-grid generator.
 *
 * This grid generator works by essentially trying to solve a grid
 * starting from no clues, and not worrying that there's more than
 * one possible solution. Unfortunately, it isn't computationally
 * feasible to do this by calling the above solver with an empty
 * grid, because that one needs to allocate a lot of scratch space
 * at every recursion level. Instead, I have a much simpler
 * algorithm which I shamelessly copied from a Python solver
 * written by Andrew Wilkinson (which is GPLed, but I've reused
 * only ideas and no code). It mostly just does the obvious
 * recursive thing: pick an empty square, put one of the possible
 * digits in it, recurse until all squares are filled, backtrack
 * and change some choices if necessary.
 *
 * The clever bit is that every time it chooses which square to
 * fill in next, it does so by counting the number of _possible_
 * numbers that can go in each square, and it prioritises so that
 * it picks a square with the _lowest_ number of possibilities. The
 * idea is that filling in lots of the obvious bits (particularly
 * any squares with only one possibility) will cut down on the list
 * of possibilities for other squares and hence reduce the enormous
 * search space as much as possible as early as possible.
 */

/*
 * Internal data structure used in gridgen to keep track of
 * progress.
 */
struct gridgen_coord { int x, y, r; };
struct gridgen_usage {
    int c, r, cr;		       /* cr == c*r */
    /* grid is a copy of the input grid, modified as we go along */
    digit *grid;
    /* row[y*cr+n-1] TRUE if digit n has been placed in row y */
    unsigned char *row;
    /* col[x*cr+n-1] TRUE if digit n has been placed in row x */
    unsigned char *col;
    /* blk[(y*c+x)*cr+n-1] TRUE if digit n has been placed in block (x,y) */
    unsigned char *blk;
    /* This lists all the empty spaces remaining in the grid. */
    struct gridgen_coord *spaces;
    int nspaces;
    /* If we need randomisation in the solve, this is our random state. */
    random_state *rs;
};

/*
 * The real recursive step in the generating function.
 */
static int gridgen_real(struct gridgen_usage *usage, digit *grid)
{
    int c = usage->c, r = usage->r, cr = usage->cr;
    int i, j, n, sx, sy, bestm, bestr, ret;
    int *digits;

    /*
     * Firstly, check for completion! If there are no spaces left
     * in the grid, we have a solution.
     */
    if (usage->nspaces == 0) {
        memcpy(grid, usage->grid, cr * cr);
	return TRUE;
    }

    /*
     * Otherwise, there must be at least one space. Find the most
     * constrained space, using the `r' field as a tie-breaker.
     */
    bestm = cr+1;		       /* so that any space will beat it */
    bestr = 0;
    i = sx = sy = -1;
    for (j = 0; j < usage->nspaces; j++) {
	int x = usage->spaces[j].x, y = usage->spaces[j].y;
	int m;

	/*
	 * Find the number of digits that could go in this space.
	 */
	m = 0;
	for (n = 0; n < cr; n++)
	    if (!usage->row[y*cr+n] && !usage->col[x*cr+n] &&
		!usage->blk[((y/c)*c+(x/r))*cr+n])
		m++;

	if (m < bestm || (m == bestm && usage->spaces[j].r < bestr)) {
	    bestm = m;
	    bestr = usage->spaces[j].r;
	    sx = x;
	    sy = y;
	    i = j;
	}
    }

    /*
     * Swap that square into the final place in the spaces array,
     * so that decrementing nspaces will remove it from the list.
     */
    if (i != usage->nspaces-1) {
	struct gridgen_coord t;
	t = usage->spaces[usage->nspaces-1];
	usage->spaces[usage->nspaces-1] = usage->spaces[i];
	usage->spaces[i] = t;
    }

    /*
     * Now we've decided which square to start our recursion at,
     * simply go through all possible values, shuffling them
     * randomly first if necessary.
     */
    digits = snewn(bestm, int);
    j = 0;
    for (n = 0; n < cr; n++)
	if (!usage->row[sy*cr+n] && !usage->col[sx*cr+n] &&
	    !usage->blk[((sy/c)*c+(sx/r))*cr+n]) {
	    digits[j++] = n+1;
	}

    if (usage->rs) {
	/* shuffle */
	for (i = j; i > 1; i--) {
	    int p = random_upto(usage->rs, i);
	    if (p != i-1) {
		int t = digits[p];
		digits[p] = digits[i-1];
		digits[i-1] = t;
	    }
	}
    }

    /* And finally, go through the digit list and actually recurse. */
    ret = FALSE;
    for (i = 0; i < j; i++) {
	n = digits[i];

	/* Update the usage structure to reflect the placing of this digit. */
	usage->row[sy*cr+n-1] = usage->col[sx*cr+n-1] =
	    usage->blk[((sy/c)*c+(sx/r))*cr+n-1] = TRUE;
	usage->grid[sy*cr+sx] = n;
	usage->nspaces--;

	/* Call the solver recursively. Stop when we find a solution. */
	if (gridgen_real(usage, grid))
            ret = TRUE;

	/* Revert the usage structure. */
	usage->row[sy*cr+n-1] = usage->col[sx*cr+n-1] =
	    usage->blk[((sy/c)*c+(sx/r))*cr+n-1] = FALSE;
	usage->grid[sy*cr+sx] = 0;
	usage->nspaces++;

        if (ret)
            break;
    }

    sfree(digits);
    return ret;
}

/*
 * Entry point to generator. You give it dimensions and a starting
 * grid, which is simply an array of cr*cr digits.
 */
static void gridgen(int c, int r, digit *grid, random_state *rs)
{
    struct gridgen_usage *usage;
    int x, y, cr = c*r;

    /*
     * Clear the grid to start with.
     */
    memset(grid, 0, cr*cr);

    /*
     * Create a gridgen_usage structure.
     */
    usage = snew(struct gridgen_usage);

    usage->c = c;
    usage->r = r;
    usage->cr = cr;

    usage->grid = snewn(cr * cr, digit);
    memcpy(usage->grid, grid, cr * cr);

    usage->row = snewn(cr * cr, unsigned char);
    usage->col = snewn(cr * cr, unsigned char);
    usage->blk = snewn(cr * cr, unsigned char);
    memset(usage->row, FALSE, cr * cr);
    memset(usage->col, FALSE, cr * cr);
    memset(usage->blk, FALSE, cr * cr);

    usage->spaces = snewn(cr * cr, struct gridgen_coord);
    usage->nspaces = 0;

    usage->rs = rs;

    /*
     * Initialise the list of grid spaces.
     */
    for (y = 0; y < cr; y++) {
	for (x = 0; x < cr; x++) {
            usage->spaces[usage->nspaces].x = x;
            usage->spaces[usage->nspaces].y = y;
            usage->spaces[usage->nspaces].r = random_bits(rs, 31);
            usage->nspaces++;
	}
    }

    /*
     * Run the real generator function.
     */
    gridgen_real(usage, grid);

    /*
     * Clean up the usage structure now we have our answer.
     */
    sfree(usage->spaces);
    sfree(usage->blk);
    sfree(usage->col);
    sfree(usage->row);
    sfree(usage->grid);
    sfree(usage);
}

/* ----------------------------------------------------------------------
 * End of grid generator code.
 */

/*
 * Check whether a grid contains a valid complete puzzle.
 */
static int check_valid(int c, int r, digit *grid)
{
    int cr = c*r;
    unsigned char *used;
    int x, y, n;

    used = snewn(cr, unsigned char);

    /*
     * Check that each row contains precisely one of everything.
     */
    for (y = 0; y < cr; y++) {
	memset(used, FALSE, cr);
	for (x = 0; x < cr; x++)
	    if (grid[y*cr+x] > 0 && grid[y*cr+x] <= cr)
		used[grid[y*cr+x]-1] = TRUE;
	for (n = 0; n < cr; n++)
	    if (!used[n]) {
		sfree(used);
		return FALSE;
	    }
    }

    /*
     * Check that each column contains precisely one of everything.
     */
    for (x = 0; x < cr; x++) {
	memset(used, FALSE, cr);
	for (y = 0; y < cr; y++)
	    if (grid[y*cr+x] > 0 && grid[y*cr+x] <= cr)
		used[grid[y*cr+x]-1] = TRUE;
	for (n = 0; n < cr; n++)
	    if (!used[n]) {
		sfree(used);
		return FALSE;
	    }
    }

    /*
     * Check that each block contains precisely one of everything.
     */
    for (x = 0; x < cr; x += r) {
	for (y = 0; y < cr; y += c) {
	    int xx, yy;
	    memset(used, FALSE, cr);
	    for (xx = x; xx < x+r; xx++)
		for (yy = 0; yy < y+c; yy++)
		    if (grid[yy*cr+xx] > 0 && grid[yy*cr+xx] <= cr)
			used[grid[yy*cr+xx]-1] = TRUE;
	    for (n = 0; n < cr; n++)
		if (!used[n]) {
		    sfree(used);
		    return FALSE;
		}
	}
    }

    sfree(used);
    return TRUE;
}

static int symmetries(game_params *params, int x, int y, int *output, int s)
{
    int c = params->c, r = params->r, cr = c*r;
    int i = 0;

#define ADD(x,y) (*output++ = (x), *output++ = (y), i++)

    ADD(x, y);

    switch (s) {
      case SYMM_NONE:
	break;			       /* just x,y is all we need */
      case SYMM_ROT2:
        ADD(cr - 1 - x, cr - 1 - y);
        break;
      case SYMM_ROT4:
        ADD(cr - 1 - y, x);
        ADD(y, cr - 1 - x);
        ADD(cr - 1 - x, cr - 1 - y);
        break;
      case SYMM_REF2:
        ADD(cr - 1 - x, y);
        break;
      case SYMM_REF2D:
        ADD(y, x);
        break;
      case SYMM_REF4:
        ADD(cr - 1 - x, y);
        ADD(x, cr - 1 - y);
        ADD(cr - 1 - x, cr - 1 - y);
        break;
      case SYMM_REF4D:
        ADD(y, x);
        ADD(cr - 1 - x, cr - 1 - y);
        ADD(cr - 1 - y, cr - 1 - x);
        break;
      case SYMM_REF8:
        ADD(cr - 1 - x, y);
        ADD(x, cr - 1 - y);
        ADD(cr - 1 - x, cr - 1 - y);
        ADD(y, x);
        ADD(y, cr - 1 - x);
        ADD(cr - 1 - y, x);
        ADD(cr - 1 - y, cr - 1 - x);
        break;
    }

#undef ADD

    return i;
}

static char *encode_solve_move(int cr, digit *grid)
{
    int i, len;
    char *ret, *p, *sep;

    /*
     * It's surprisingly easy to work out _exactly_ how long this
     * string needs to be. To decimal-encode all the numbers from 1
     * to n:
     * 
     *  - every number has a units digit; total is n.
     *  - all numbers above 9 have a tens digit; total is max(n-9,0).
     *  - all numbers above 99 have a hundreds digit; total is max(n-99,0).
     *  - and so on.
     */
    len = 0;
    for (i = 1; i <= cr; i *= 10)
	len += max(cr - i + 1, 0);
    len += cr;		       /* don't forget the commas */
    len *= cr;		       /* there are cr rows of these */

    /*
     * Now len is one bigger than the total size of the
     * comma-separated numbers (because we counted an
     * additional leading comma). We need to have a leading S
     * and a trailing NUL, so we're off by one in total.
     */
    len++;

    ret = snewn(len, char);
    p = ret;
    *p++ = 'S';
    sep = "";
    for (i = 0; i < cr*cr; i++) {
	p += sprintf(p, "%s%d", sep, grid[i]);
	sep = ",";
    }
    *p++ = '\0';
    assert(p - ret == len);

    return ret;
}

static char *new_game_desc(game_params *params, random_state *rs,
			   char **aux, int interactive)
{
    int c = params->c, r = params->r, cr = c*r;
    int area = cr*cr;
    digit *grid, *grid2;
    struct xy { int x, y; } *locs;
    int nlocs;
    char *desc;
    int coords[16], ncoords;
    int *symmclasses, nsymmclasses;
    int maxdiff, recursing;

    /*
     * Adjust the maximum difficulty level to be consistent with
     * the puzzle size: all 2x2 puzzles appear to be Trivial
     * (DIFF_BLOCK) so we cannot hold out for even a Basic
     * (DIFF_SIMPLE) one.
     */
    maxdiff = params->diff;
    if (c == 2 && r == 2)
        maxdiff = DIFF_BLOCK;

    grid = snewn(area, digit);
    locs = snewn(area, struct xy);
    grid2 = snewn(area, digit);

    /*
     * Find the set of equivalence classes of squares permitted
     * by the selected symmetry. We do this by enumerating all
     * the grid squares which have no symmetric companion
     * sorting lower than themselves.
     */
    nsymmclasses = 0;
    symmclasses = snewn(cr * cr, int);
    {
        int x, y;

        for (y = 0; y < cr; y++)
            for (x = 0; x < cr; x++) {
                int i = y*cr+x;
                int j;

                ncoords = symmetries(params, x, y, coords, params->symm);
                for (j = 0; j < ncoords; j++)
                    if (coords[2*j+1]*cr+coords[2*j] < i)
                        break;
                if (j == ncoords)
                    symmclasses[nsymmclasses++] = i;
            }
    }

    /*
     * Loop until we get a grid of the required difficulty. This is
     * nasty, but it seems to be unpleasantly hard to generate
     * difficult grids otherwise.
     */
    do {
        /*
         * Generate a random solved state.
         */
        gridgen(c, r, grid, rs);
        assert(check_valid(c, r, grid));

	/*
	 * Save the solved grid in aux.
	 */
	{
	    /*
	     * We might already have written *aux the last time we
	     * went round this loop, in which case we should free
	     * the old aux before overwriting it with the new one.
	     */
            if (*aux) {
		sfree(*aux);
            }

            *aux = encode_solve_move(cr, grid);
	}

        /*
         * Now we have a solved grid, start removing things from it
         * while preserving solubility.
         */
	recursing = FALSE;
        while (1) {
            int x, y, i, j;

            /*
             * Iterate over the grid and enumerate all the filled
             * squares we could empty.
             */
            nlocs = 0;

            for (i = 0; i < nsymmclasses; i++) {
                x = symmclasses[i] % cr;
                y = symmclasses[i] / cr;
                if (grid[y*cr+x]) {
                    locs[nlocs].x = x;
                    locs[nlocs].y = y;
                    nlocs++;
                }
            }

            /*
             * Now shuffle that list.
             */
            for (i = nlocs; i > 1; i--) {
                int p = random_upto(rs, i);
                if (p != i-1) {
                    struct xy t = locs[p];
                    locs[p] = locs[i-1];
                    locs[i-1] = t;
                }
            }

            /*
             * Now loop over the shuffled list and, for each element,
             * see whether removing that element (and its reflections)
             * from the grid will still leave the grid soluble by
             * solver.
             */
            for (i = 0; i < nlocs; i++) {
		int ret;

                x = locs[i].x;
                y = locs[i].y;

                memcpy(grid2, grid, area);
                ncoords = symmetries(params, x, y, coords, params->symm);
                for (j = 0; j < ncoords; j++)
                    grid2[coords[2*j+1]*cr+coords[2*j]] = 0;

		ret = solver(c, r, grid2, NULL, maxdiff);
                if (ret != DIFF_IMPOSSIBLE && ret != DIFF_AMBIGUOUS) {
                    for (j = 0; j < ncoords; j++)
                        grid[coords[2*j+1]*cr+coords[2*j]] = 0;
                    break;
                }
            }

            if (i == nlocs) {
                /*
                 * There was nothing we could remove without
                 * destroying solvability. Give up.
                 */
                break;
            }
        }

        memcpy(grid2, grid, area);
    } while (solver(c, r, grid2, NULL, maxdiff) < maxdiff);

    sfree(grid2);
    sfree(locs);

    sfree(symmclasses);

    /*
     * Now we have the grid as it will be presented to the user.
     * Encode it in a game desc.
     */
    {
	char *p;
	int run, i;

	desc = snewn(5 * area, char);
	p = desc;
	run = 0;
	for (i = 0; i <= area; i++) {
	    int n = (i < area ? grid[i] : -1);

	    if (!n)
		run++;
	    else {
		if (run) {
		    while (run > 0) {
			int c = 'a' - 1 + run;
			if (run > 26)
			    c = 'z';
			*p++ = c;
			run -= c - ('a' - 1);
		    }
		} else {
		    /*
		     * If there's a number in the very top left or
		     * bottom right, there's no point putting an
		     * unnecessary _ before or after it.
		     */
		    if (p > desc && n > 0)
			*p++ = '_';
		}
		if (n > 0)
		    p += sprintf(p, "%d", n);
		run = 0;
	    }
	}
	assert(p - desc < 5 * area);
	*p++ = '\0';
	desc = sresize(desc, p - desc, char);
    }

    sfree(grid);

    return desc;
}

static char *validate_desc(game_params *params, char *desc)
{
    int area = params->r * params->r * params->c * params->c;
    int squares = 0;

    while (*desc) {
        int n = *desc++;
        if (n >= 'a' && n <= 'z') {
            squares += n - 'a' + 1;
        } else if (n == '_') {
            /* do nothing */;
        } else if (n > '0' && n <= '9') {
            squares++;
            while (*desc >= '0' && *desc <= '9')
                desc++;
        } else
            return "Invalid character in game description";
    }

    if (squares < area)
        return "Not enough data to fill grid";

    if (squares > area)
        return "Too much data to fit in grid";

    return NULL;
}

static game_state *new_game(midend_data *me, game_params *params, char *desc)
{
    game_state *state = snew(game_state);
    int c = params->c, r = params->r, cr = c*r, area = cr * cr;
    int i;

    state->c = params->c;
    state->r = params->r;

    state->grid = snewn(area, digit);
    state->pencil = snewn(area * cr, unsigned char);
    memset(state->pencil, 0, area * cr);
    state->immutable = snewn(area, unsigned char);
    memset(state->immutable, FALSE, area);

    state->completed = state->cheated = FALSE;

    i = 0;
    while (*desc) {
        int n = *desc++;
        if (n >= 'a' && n <= 'z') {
            int run = n - 'a' + 1;
            assert(i + run <= area);
            while (run-- > 0)
                state->grid[i++] = 0;
        } else if (n == '_') {
            /* do nothing */;
        } else if (n > '0' && n <= '9') {
            assert(i < area);
	    state->immutable[i] = TRUE;
            state->grid[i++] = atoi(desc-1);
            while (*desc >= '0' && *desc <= '9')
                desc++;
        } else {
            assert(!"We can't get here");
        }
    }
    assert(i == area);

    return state;
}

static game_state *dup_game(game_state *state)
{
    game_state *ret = snew(game_state);
    int c = state->c, r = state->r, cr = c*r, area = cr * cr;

    ret->c = state->c;
    ret->r = state->r;

    ret->grid = snewn(area, digit);
    memcpy(ret->grid, state->grid, area);

    ret->pencil = snewn(area * cr, unsigned char);
    memcpy(ret->pencil, state->pencil, area * cr);

    ret->immutable = snewn(area, unsigned char);
    memcpy(ret->immutable, state->immutable, area);

    ret->completed = state->completed;
    ret->cheated = state->cheated;

    return ret;
}

static void free_game(game_state *state)
{
    sfree(state->immutable);
    sfree(state->pencil);
    sfree(state->grid);
    sfree(state);
}

static char *solve_game(game_state *state, game_state *currstate,
			char *ai, char **error)
{
    int c = state->c, r = state->r, cr = c*r;
    char *ret;
    digit *grid;
    int solve_ret;

    /*
     * If we already have the solution in ai, save ourselves some
     * time.
     */
    if (ai)
        return dupstr(ai);

    grid = snewn(cr*cr, digit);
    memcpy(grid, state->grid, cr*cr);
    solve_ret = solver(c, r, grid, NULL, DIFF_RECURSIVE);

    *error = NULL;

    if (solve_ret == DIFF_IMPOSSIBLE)
	*error = "No solution exists for this puzzle";
    else if (solve_ret == DIFF_AMBIGUOUS)
	*error = "Multiple solutions exist for this puzzle";

    if (*error) {
        sfree(grid);
        return NULL;
    }

    ret = encode_solve_move(cr, grid);

    sfree(grid);

    return ret;
}

static char *grid_text_format(int c, int r, digit *grid)
{
    int cr = c*r;
    int x, y;
    int maxlen;
    char *ret, *p;

    /*
     * There are cr lines of digits, plus r-1 lines of block
     * separators. Each line contains cr digits, cr-1 separating
     * spaces, and c-1 two-character block separators. Thus, the
     * total length of a line is 2*cr+2*c-3 (not counting the
     * newline), and there are cr+r-1 of them.
     */
    maxlen = (cr+r-1) * (2*cr+2*c-2);
    ret = snewn(maxlen+1, char);
    p = ret;

    for (y = 0; y < cr; y++) {
        for (x = 0; x < cr; x++) {
            int ch = grid[y * cr + x];
            if (ch == 0)
                ch = ' ';
            else if (ch <= 9)
                ch = '0' + ch;
            else
                ch = 'a' + ch-10;
            *p++ = ch;
            if (x+1 < cr) {
		*p++ = ' ';
                if ((x+1) % r == 0) {
                    *p++ = '|';
		    *p++ = ' ';
		}
            }
        }
	*p++ = '\n';
        if (y+1 < cr && (y+1) % c == 0) {
            for (x = 0; x < cr; x++) {
                *p++ = '-';
                if (x+1 < cr) {
		    *p++ = '-';
                    if ((x+1) % r == 0) {
			*p++ = '+';
			*p++ = '-';
		    }
                }
            }
	    *p++ = '\n';
        }
    }

    assert(p - ret == maxlen);
    *p = '\0';
    return ret;
}

static char *game_text_format(game_state *state)
{
    return grid_text_format(state->c, state->r, state->grid);
}

struct game_ui {
    /*
     * These are the coordinates of the currently highlighted
     * square on the grid, or -1,-1 if there isn't one. When there
     * is, pressing a valid number or letter key or Space will
     * enter that number or letter in the grid.
     */
    int hx, hy;
    /*
     * This indicates whether the current highlight is a
     * pencil-mark one or a real one.
     */
    int hpencil;
};

static game_ui *new_ui(game_state *state)
{
    game_ui *ui = snew(game_ui);

    ui->hx = ui->hy = -1;
    ui->hpencil = 0;

    return ui;
}

static void free_ui(game_ui *ui)
{
    sfree(ui);
}

static char *encode_ui(game_ui *ui)
{
    return NULL;
}

static void decode_ui(game_ui *ui, char *encoding)
{
}

static void game_changed_state(game_ui *ui, game_state *oldstate,
                               game_state *newstate)
{
    int c = newstate->c, r = newstate->r, cr = c*r;
    /*
     * We prevent pencil-mode highlighting of a filled square. So
     * if the user has just filled in a square which we had a
     * pencil-mode highlight in (by Undo, or by Redo, or by Solve),
     * then we cancel the highlight.
     */
    if (ui->hx >= 0 && ui->hy >= 0 && ui->hpencil &&
        newstate->grid[ui->hy * cr + ui->hx] != 0) {
        ui->hx = ui->hy = -1;
    }
}

struct game_drawstate {
    int started;
    int c, r, cr;
    int tilesize;
    digit *grid;
    unsigned char *pencil;
    unsigned char *hl;
    /* This is scratch space used within a single call to game_redraw. */
    int *entered_items;
};

static char *interpret_move(game_state *state, game_ui *ui, game_drawstate *ds,
			    int x, int y, int button)
{
    int c = state->c, r = state->r, cr = c*r;
    int tx, ty;
    char buf[80];

    button &= ~MOD_MASK;

    tx = (x + TILE_SIZE - BORDER) / TILE_SIZE - 1;
    ty = (y + TILE_SIZE - BORDER) / TILE_SIZE - 1;

    if (tx >= 0 && tx < cr && ty >= 0 && ty < cr) {
        if (button == LEFT_BUTTON) {
            if (state->immutable[ty*cr+tx]) {
                ui->hx = ui->hy = -1;
            } else if (tx == ui->hx && ty == ui->hy && ui->hpencil == 0) {
                ui->hx = ui->hy = -1;
            } else {
                ui->hx = tx;
                ui->hy = ty;
                ui->hpencil = 0;
            }
            return "";		       /* UI activity occurred */
        }
        if (button == RIGHT_BUTTON) {
            /*
             * Pencil-mode highlighting for non filled squares.
             */
            if (state->grid[ty*cr+tx] == 0) {
                if (tx == ui->hx && ty == ui->hy && ui->hpencil) {
                    ui->hx = ui->hy = -1;
                } else {
                    ui->hpencil = 1;
                    ui->hx = tx;
                    ui->hy = ty;
                }
            } else {
                ui->hx = ui->hy = -1;
            }
            return "";		       /* UI activity occurred */
        }
    }

    if (ui->hx != -1 && ui->hy != -1 &&
	((button >= '1' && button <= '9' && button - '0' <= cr) ||
	 (button >= 'a' && button <= 'z' && button - 'a' + 10 <= cr) ||
	 (button >= 'A' && button <= 'Z' && button - 'A' + 10 <= cr) ||
	 button == ' ')) {
	int n = button - '0';
	if (button >= 'A' && button <= 'Z')
	    n = button - 'A' + 10;
	if (button >= 'a' && button <= 'z')
	    n = button - 'a' + 10;
	if (button == ' ')
	    n = 0;

        /*
         * Can't overwrite this square. In principle this shouldn't
         * happen anyway because we should never have even been
         * able to highlight the square, but it never hurts to be
         * careful.
         */
	if (state->immutable[ui->hy*cr+ui->hx])
	    return NULL;

        /*
         * Can't make pencil marks in a filled square. In principle
         * this shouldn't happen anyway because we should never
         * have even been able to pencil-highlight the square, but
         * it never hurts to be careful.
         */
        if (ui->hpencil && state->grid[ui->hy*cr+ui->hx])
            return NULL;

	sprintf(buf, "%c%d,%d,%d",
		(char)(ui->hpencil && n > 0 ? 'P' : 'R'), ui->hx, ui->hy, n);

	ui->hx = ui->hy = -1;

	return dupstr(buf);
    }

    return NULL;
}

static game_state *execute_move(game_state *from, char *move)
{
    int c = from->c, r = from->r, cr = c*r;
    game_state *ret;
    int x, y, n;

    if (move[0] == 'S') {
	char *p;

	ret = dup_game(from);
	ret->completed = ret->cheated = TRUE;

	p = move+1;
	for (n = 0; n < cr*cr; n++) {
	    ret->grid[n] = atoi(p);

	    if (!*p || ret->grid[n] < 1 || ret->grid[n] > cr) {
		free_game(ret);
		return NULL;
	    }

	    while (*p && isdigit((unsigned char)*p)) p++;
	    if (*p == ',') p++;
	}

	return ret;
    } else if ((move[0] == 'P' || move[0] == 'R') &&
	sscanf(move+1, "%d,%d,%d", &x, &y, &n) == 3 &&
	x >= 0 && x < cr && y >= 0 && y < cr && n >= 0 && n <= cr) {

	ret = dup_game(from);
        if (move[0] == 'P' && n > 0) {
            int index = (y*cr+x) * cr + (n-1);
            ret->pencil[index] = !ret->pencil[index];
        } else {
            ret->grid[y*cr+x] = n;
            memset(ret->pencil + (y*cr+x)*cr, 0, cr);

            /*
             * We've made a real change to the grid. Check to see
             * if the game has been completed.
             */
            if (!ret->completed && check_valid(c, r, ret->grid)) {
                ret->completed = TRUE;
            }
        }
	return ret;
    } else
	return NULL;		       /* couldn't parse move string */
}

/* ----------------------------------------------------------------------
 * Drawing routines.
 */

#define SIZE(cr) ((cr) * TILE_SIZE + 2*BORDER + 1)
#define GETTILESIZE(cr, w) ( (double)(w-1) / (double)(cr+1) )

static void game_compute_size(game_params *params, int tilesize,
			      int *x, int *y)
{
    /* Ick: fake up `ds->tilesize' for macro expansion purposes */
    struct { int tilesize; } ads, *ds = &ads;
    ads.tilesize = tilesize;

    *x = SIZE(params->c * params->r);
    *y = SIZE(params->c * params->r);
}

static void game_set_size(game_drawstate *ds, game_params *params,
			  int tilesize)
{
    ds->tilesize = tilesize;
}

static float *game_colours(frontend *fe, game_state *state, int *ncolours)
{
    float *ret = snewn(3 * NCOLOURS, float);

    frontend_default_colour(fe, &ret[COL_BACKGROUND * 3]);

    ret[COL_GRID * 3 + 0] = 0.0F;
    ret[COL_GRID * 3 + 1] = 0.0F;
    ret[COL_GRID * 3 + 2] = 0.0F;

    ret[COL_CLUE * 3 + 0] = 0.0F;
    ret[COL_CLUE * 3 + 1] = 0.0F;
    ret[COL_CLUE * 3 + 2] = 0.0F;

    ret[COL_USER * 3 + 0] = 0.0F;
    ret[COL_USER * 3 + 1] = 0.6F * ret[COL_BACKGROUND * 3 + 1];
    ret[COL_USER * 3 + 2] = 0.0F;

    ret[COL_HIGHLIGHT * 3 + 0] = 0.85F * ret[COL_BACKGROUND * 3 + 0];
    ret[COL_HIGHLIGHT * 3 + 1] = 0.85F * ret[COL_BACKGROUND * 3 + 1];
    ret[COL_HIGHLIGHT * 3 + 2] = 0.85F * ret[COL_BACKGROUND * 3 + 2];

    ret[COL_ERROR * 3 + 0] = 1.0F;
    ret[COL_ERROR * 3 + 1] = 0.0F;
    ret[COL_ERROR * 3 + 2] = 0.0F;

    ret[COL_PENCIL * 3 + 0] = 0.5F * ret[COL_BACKGROUND * 3 + 0];
    ret[COL_PENCIL * 3 + 1] = 0.5F * ret[COL_BACKGROUND * 3 + 1];
    ret[COL_PENCIL * 3 + 2] = ret[COL_BACKGROUND * 3 + 2];

    *ncolours = NCOLOURS;
    return ret;
}

static game_drawstate *game_new_drawstate(game_state *state)
{
    struct game_drawstate *ds = snew(struct game_drawstate);
    int c = state->c, r = state->r, cr = c*r;

    ds->started = FALSE;
    ds->c = c;
    ds->r = r;
    ds->cr = cr;
    ds->grid = snewn(cr*cr, digit);
    memset(ds->grid, 0, cr*cr);
    ds->pencil = snewn(cr*cr*cr, digit);
    memset(ds->pencil, 0, cr*cr*cr);
    ds->hl = snewn(cr*cr, unsigned char);
    memset(ds->hl, 0, cr*cr);
    ds->entered_items = snewn(cr*cr, int);
    ds->tilesize = 0;                  /* not decided yet */
    return ds;
}

static void game_free_drawstate(game_drawstate *ds)
{
    sfree(ds->hl);
    sfree(ds->pencil);
    sfree(ds->grid);
    sfree(ds->entered_items);
    sfree(ds);
}

static void draw_number(frontend *fe, game_drawstate *ds, game_state *state,
			int x, int y, int hl)
{
    int c = state->c, r = state->r, cr = c*r;
    int tx, ty;
    int cx, cy, cw, ch;
    char str[2];

    if (ds->grid[y*cr+x] == state->grid[y*cr+x] &&
        ds->hl[y*cr+x] == hl &&
        !memcmp(ds->pencil+(y*cr+x)*cr, state->pencil+(y*cr+x)*cr, cr))
	return;			       /* no change required */

    tx = BORDER + x * TILE_SIZE + 2;
    ty = BORDER + y * TILE_SIZE + 2;

    cx = tx;
    cy = ty;
    cw = TILE_SIZE-3;
    ch = TILE_SIZE-3;

    if (x % r)
	cx--, cw++;
    if ((x+1) % r)
	cw++;
    if (y % c)
	cy--, ch++;
    if ((y+1) % c)
	ch++;

    clip(fe, cx, cy, cw, ch);

    /* background needs erasing */
    draw_rect(fe, cx, cy, cw, ch, (hl & 15) == 1 ? COL_HIGHLIGHT : COL_BACKGROUND);

    /* pencil-mode highlight */
    if ((hl & 15) == 2) {
        int coords[6];
        coords[0] = cx;
        coords[1] = cy;
        coords[2] = cx+cw/2;
        coords[3] = cy;
        coords[4] = cx;
        coords[5] = cy+ch/2;
        draw_polygon(fe, coords, 3, COL_HIGHLIGHT, COL_HIGHLIGHT);
    }

    /* new number needs drawing? */
    if (state->grid[y*cr+x]) {
	str[1] = '\0';
	str[0] = state->grid[y*cr+x] + '0';
	if (str[0] > '9')
	    str[0] += 'a' - ('9'+1);
	draw_text(fe, tx + TILE_SIZE/2, ty + TILE_SIZE/2,
		  FONT_VARIABLE, TILE_SIZE/2, ALIGN_VCENTRE | ALIGN_HCENTRE,
		  state->immutable[y*cr+x] ? COL_CLUE : (hl & 16) ? COL_ERROR : COL_USER, str);
    } else {
        int i, j, npencil;
	int pw, ph, pmax, fontsize;

        /* count the pencil marks required */
        for (i = npencil = 0; i < cr; i++)
            if (state->pencil[(y*cr+x)*cr+i])
		npencil++;

	/*
	 * It's not sensible to arrange pencil marks in the same
	 * layout as the squares within a block, because this leads
	 * to the font being too small. Instead, we arrange pencil
	 * marks in the nearest thing we can to a square layout,
	 * and we adjust the square layout depending on the number
	 * of pencil marks in the square.
	 */
	for (pw = 1; pw * pw < npencil; pw++);
	if (pw < 3) pw = 3;	       /* otherwise it just looks _silly_ */
	ph = (npencil + pw - 1) / pw;
	if (ph < 2) ph = 2;	       /* likewise */
	pmax = max(pw, ph);
	fontsize = TILE_SIZE/(pmax*(11-pmax)/8);

        for (i = j = 0; i < cr; i++)
            if (state->pencil[(y*cr+x)*cr+i]) {
                int dx = j % pw, dy = j / pw;

                str[1] = '\0';
                str[0] = i + '1';
                if (str[0] > '9')
                    str[0] += 'a' - ('9'+1);
                draw_text(fe, tx + (4*dx+3) * TILE_SIZE / (4*pw+2),
                          ty + (4*dy+3) * TILE_SIZE / (4*ph+2),
                          FONT_VARIABLE, fontsize,
                          ALIGN_VCENTRE | ALIGN_HCENTRE, COL_PENCIL, str);
                j++;
            }
    }

    unclip(fe);

    draw_update(fe, cx, cy, cw, ch);

    ds->grid[y*cr+x] = state->grid[y*cr+x];
    memcpy(ds->pencil+(y*cr+x)*cr, state->pencil+(y*cr+x)*cr, cr);
    ds->hl[y*cr+x] = hl;
}

static void game_redraw(frontend *fe, game_drawstate *ds, game_state *oldstate,
			game_state *state, int dir, game_ui *ui,
			float animtime, float flashtime)
{
    int c = state->c, r = state->r, cr = c*r;
    int x, y;

    if (!ds->started) {
	/*
	 * The initial contents of the window are not guaranteed
	 * and can vary with front ends. To be on the safe side,
	 * all games should start by drawing a big
	 * background-colour rectangle covering the whole window.
	 */
	draw_rect(fe, 0, 0, SIZE(cr), SIZE(cr), COL_BACKGROUND);

	/*
	 * Draw the grid.
	 */
	for (x = 0; x <= cr; x++) {
	    int thick = (x % r ? 0 : 1);
	    draw_rect(fe, BORDER + x*TILE_SIZE - thick, BORDER-1,
		      1+2*thick, cr*TILE_SIZE+3, COL_GRID);
	}
	for (y = 0; y <= cr; y++) {
	    int thick = (y % c ? 0 : 1);
	    draw_rect(fe, BORDER-1, BORDER + y*TILE_SIZE - thick,
		      cr*TILE_SIZE+3, 1+2*thick, COL_GRID);
	}
    }

    /*
     * This array is used to keep track of rows, columns and boxes
     * which contain a number more than once.
     */
    for (x = 0; x < cr * cr; x++)
	ds->entered_items[x] = 0;
    for (x = 0; x < cr; x++)
	for (y = 0; y < cr; y++) {
	    digit d = state->grid[y*cr+x];
	    if (d) {
		int box = (x/r)+(y/c)*c;
		ds->entered_items[x*cr+d-1] |= ((ds->entered_items[x*cr+d-1] & 1) << 1) | 1;
		ds->entered_items[y*cr+d-1] |= ((ds->entered_items[y*cr+d-1] & 4) << 1) | 4;
		ds->entered_items[box*cr+d-1] |= ((ds->entered_items[box*cr+d-1] & 16) << 1) | 16;
	    }
	}

    /*
     * Draw any numbers which need redrawing.
     */
    for (x = 0; x < cr; x++) {
	for (y = 0; y < cr; y++) {
            int highlight = 0;
            digit d = state->grid[y*cr+x];

            if (flashtime > 0 &&
                (flashtime <= FLASH_TIME/3 ||
                 flashtime >= FLASH_TIME*2/3))
                highlight = 1;

            /* Highlight active input areas. */
            if (x == ui->hx && y == ui->hy)
                highlight = ui->hpencil ? 2 : 1;

	    /* Mark obvious errors (ie, numbers which occur more than once
	     * in a single row, column, or box). */
	    if (d && ((ds->entered_items[x*cr+d-1] & 2) ||
		      (ds->entered_items[y*cr+d-1] & 8) ||
		      (ds->entered_items[((x/r)+(y/c)*c)*cr+d-1] & 32)))
		highlight |= 16;

	    draw_number(fe, ds, state, x, y, highlight);
	}
    }

    /*
     * Update the _entire_ grid if necessary.
     */
    if (!ds->started) {
	draw_update(fe, 0, 0, SIZE(cr), SIZE(cr));
	ds->started = TRUE;
    }
}

static float game_anim_length(game_state *oldstate, game_state *newstate,
			      int dir, game_ui *ui)
{
    return 0.0F;
}

static float game_flash_length(game_state *oldstate, game_state *newstate,
			       int dir, game_ui *ui)
{
    if (!oldstate->completed && newstate->completed &&
	!oldstate->cheated && !newstate->cheated)
        return FLASH_TIME;
    return 0.0F;
}

static int game_wants_statusbar(void)
{
    return FALSE;
}

static int game_timing_state(game_state *state, game_ui *ui)
{
    return TRUE;
}

#ifdef COMBINED
#define thegame solo
#endif

const struct game thegame = {
    "Solo", "games.solo",
    default_params,
    game_fetch_preset,
    decode_params,
    encode_params,
    free_params,
    dup_params,
    TRUE, game_configure, custom_params,
    validate_params,
    new_game_desc,
    validate_desc,
    new_game,
    dup_game,
    free_game,
    TRUE, solve_game,
    TRUE, game_text_format,
    new_ui,
    free_ui,
    encode_ui,
    decode_ui,
    game_changed_state,
    interpret_move,
    execute_move,
    PREFERRED_TILE_SIZE, game_compute_size, game_set_size,
    game_colours,
    game_new_drawstate,
    game_free_drawstate,
    game_redraw,
    game_anim_length,
    game_flash_length,
    game_wants_statusbar,
    FALSE, game_timing_state,
    0,				       /* mouse_priorities */
};

#ifdef STANDALONE_SOLVER

/*
 * gcc -DSTANDALONE_SOLVER -o solosolver solo.c malloc.c
 */

void frontend_default_colour(frontend *fe, float *output) {}
void draw_text(frontend *fe, int x, int y, int fonttype, int fontsize,
               int align, int colour, char *text) {}
void draw_rect(frontend *fe, int x, int y, int w, int h, int colour) {}
void draw_line(frontend *fe, int x1, int y1, int x2, int y2, int colour) {}
void draw_polygon(frontend *fe, int *coords, int npoints,
                  int fillcolour, int outlinecolour) {}
void clip(frontend *fe, int x, int y, int w, int h) {}
void unclip(frontend *fe) {}
void start_draw(frontend *fe) {}
void draw_update(frontend *fe, int x, int y, int w, int h) {}
void end_draw(frontend *fe) {}
unsigned long random_bits(random_state *state, int bits)
{ assert(!"Shouldn't get randomness"); return 0; }
unsigned long random_upto(random_state *state, unsigned long limit)
{ assert(!"Shouldn't get randomness"); return 0; }

void fatal(char *fmt, ...)
{
    va_list ap;

    fprintf(stderr, "fatal error: ");

    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);

    fprintf(stderr, "\n");
    exit(1);
}

int main(int argc, char **argv)
{
    game_params *p;
    game_state *s;
    char *id = NULL, *desc, *err;
    int grade = FALSE;
    int ret;

    while (--argc > 0) {
        char *p = *++argv;
        if (!strcmp(p, "-v")) {
            solver_show_working = TRUE;
        } else if (!strcmp(p, "-g")) {
            grade = TRUE;
        } else if (*p == '-') {
            fprintf(stderr, "%s: unrecognised option `%s'\n", argv[0], p);
            return 1;
        } else {
            id = p;
        }
    }

    if (!id) {
        fprintf(stderr, "usage: %s [-g | -v] <game_id>\n", argv[0]);
        return 1;
    }

    desc = strchr(id, ':');
    if (!desc) {
        fprintf(stderr, "%s: game id expects a colon in it\n", argv[0]);
        return 1;
    }
    *desc++ = '\0';

    p = default_params();
    decode_params(p, id);
    err = validate_desc(p, desc);
    if (err) {
        fprintf(stderr, "%s: %s\n", argv[0], err);
        return 1;
    }
    s = new_game(NULL, p, desc);

    ret = solver(p->c, p->r, s->grid, NULL, DIFF_RECURSIVE);
    if (grade) {
	printf("Difficulty rating: %s\n",
	       ret==DIFF_BLOCK ? "Trivial (blockwise positional elimination only)":
	       ret==DIFF_SIMPLE ? "Basic (row/column/number elimination required)":
	       ret==DIFF_INTERSECT ? "Intermediate (intersectional analysis required)":
	       ret==DIFF_SET ? "Advanced (set elimination required)":
	       ret==DIFF_RECURSIVE ? "Unreasonable (guesswork and backtracking required)":
	       ret==DIFF_AMBIGUOUS ? "Ambiguous (multiple solutions exist)":
	       ret==DIFF_IMPOSSIBLE ? "Impossible (no solution exists)":
	       "INTERNAL ERROR: unrecognised difficulty code");
    } else {
        printf("%s\n", grid_text_format(p->c, p->r, s->grid));
    }

    return 0;
}

#endif
