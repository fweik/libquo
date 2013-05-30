/**
 * Copyright (c) 2013      Los Alamos National Security, LLC.
 *                         All rights reserved.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "quo.h"

#include <stdio.h>
#include <stdlib.h>
#ifdef HAVE_STDBOOL_H
#include <stdbool.h>
#endif
#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#include "mpi.h"

/**
 * libquo demo code. enjoy.
 */

/**
 * SUGGESTED USE
 */

typedef struct context_t {
    /* my rank */
    int rank;
    /* number of ranks in MPI_COMM_WORLD */
    int nranks;
    /* number of nodes in our job */
    int nnodes;
    /* number of ranks that share this node with me (includes myself) */
    int nnoderanks;
    /* my node rank */
    int noderank;
    /* whether or not mpi is initialized */
    bool mpi_inited;
    /* number of sockets on the node */
    int nsockets;
    /* number of cores on the node */
    int ncores;
    /* number of pus (processing units - e.g hw threads) */
    int npus;
    /* quo major version */
    int qv;
    /* quo minor version */
    int qsv;
    /* pointer to initial stringification of our cpuset */
    char *cbindstr;
    /* flag indicating whether or not we are initially bound */
    bool bound;
    /* a pointer to our quo context (the thing that gets passed around all over
     * the place). filler words that make this comment line look mo better... */
    quo_t *quo;
} context_t;

static int
fini(context_t *c)
{
    if (!c) return 1;
    if (QUO_SUCCESS != quo_destruct(c->quo)) return 1;
    /* finalize mpi AFTER quo_destruct - we may mpi in our destruct */
    if (c->mpi_inited) MPI_Finalize();
    if (c->cbindstr) free(c->cbindstr);
    free(c);
    return 0;
}

/**
 * i'm being really sloppy here. ideally, one should probably save the rc and
 * then display or do some other cool thing with it. don't be like this code. if
 * quo_construct or quo_init fail, then someone using this could just continue
 * without the awesomeness that is libquo. they cleanup after themselves, so
 * things *should* be in an okay state after an early failure. the failures may
 * be part of a larger problem, however. */
static int
init(context_t **c)
{
    context_t *newc = NULL;
    /* alloc our context */
    if (NULL == (newc = calloc(1, sizeof(*newc)))) return 1;
    /* libquo requires that MPI be initialized before its init is called */
    if (MPI_SUCCESS != MPI_Init(NULL, NULL)) return 1;
    /* gather some basic job info from our mpi lib */
    if (MPI_SUCCESS != MPI_Comm_size(MPI_COMM_WORLD, &(newc->nranks))) goto err;
    /* ...and more */
    if (MPI_SUCCESS != MPI_Comm_rank(MPI_COMM_WORLD, &(newc->rank))) goto err;
    /* can be called at any point -- even before init and construct. */
    if (QUO_SUCCESS != quo_version(&(newc->qv), &(newc->qsv))) goto err;
    /* cheap call -- must be called before init. */
    if (QUO_SUCCESS != quo_construct(&(newc->quo))) goto err;
    /* relatively expensive call. you only really want to do this once at the
     * beginning of time and pass the context all over the place within your
     * code. */
    if (QUO_SUCCESS != quo_init(newc->quo)) goto err;
    newc->mpi_inited = true;
    *c = newc;
    return 0;
err:
    (void)fini(newc);
    return 1;
}

/**
 * gather system and job info from libquo.
 */
static int
sys_grok(context_t *c)
{
    char *bad_func = NULL;

    if (QUO_SUCCESS != quo_nsockets(c->quo, &c->nsockets)) {
        bad_func = "quo_nsockets";
        goto out;
    }
    if (QUO_SUCCESS != quo_ncores(c->quo, &c->ncores)) {
        bad_func = "quo_ncores";
        goto out;
    }
    if (QUO_SUCCESS != quo_npus(c->quo, &c->npus)) {
        bad_func = "quo_npus";
        goto out;
    }
    if (QUO_SUCCESS != quo_bound(c->quo, &c->bound)) {
        bad_func = "quo_bound";
        goto out;
    }
    if (QUO_SUCCESS != quo_stringify_cbind(c->quo, &c->cbindstr)) {
        bad_func = "quo_stringify_cbind";
        goto out;
    }
    if (QUO_SUCCESS != quo_nnodes(c->quo, &c->nnodes)) {
        bad_func = "quo_nnodes";
        goto out;
    }
    if (QUO_SUCCESS != quo_nnoderanks(c->quo, &c->nnoderanks)) {
        bad_func = "quo_nnoderanks";
        goto out;
    }
    if (QUO_SUCCESS != quo_noderank(c->quo, &c->noderank)) {
        bad_func = "quo_noderank";
        goto out;
    }
out:
    if (bad_func) {
        fprintf(stderr, "%s: %s failure :-(\n", __func__, bad_func);
        return 1;
    }
    return 0;
}

static int
emit_bind_state(const context_t *c)
{
    char *cbindstr = NULL, *bad_func = NULL;
    bool bound = false;

    /* for nice output --- not really needed */
    MPI_Barrier(MPI_COMM_WORLD);
    if (QUO_SUCCESS != quo_stringify_cbind(c->quo, &cbindstr)) {
        bad_func = "quo_stringify_cbind";
        goto out;
    }
    if (QUO_SUCCESS != quo_bound(c->quo, &bound)) {
        bad_func = "quo_bound";
        goto out;
    }
    printf("### process %d rank %d [%s] bound: %s\n",
           (int)getpid(), c->rank, cbindstr, bound ? "true" : "false");
    fflush(stdout);
out:
    /* for nice output --- not really needed */
    MPI_Barrier(MPI_COMM_WORLD);
    if (cbindstr) free(cbindstr);
    if (bad_func) {
        fprintf(stderr, "%s: %s failure :-(\n", __func__, bad_func);
        return 1;
    }
    return 0;
}

static int
emit_node_basics(const context_t *c)
{
    /* one proc per node will emit this info */
    if (0 == c->noderank) {
        printf("### quo version: %d.%d ###\n", c->qv, c->qsv);
        printf("### nnodes: %d\n", c->nnodes);
        printf("### nnoderanks: %d\n", c->nnoderanks);
        printf("### nsockets: %d\n", c->nsockets);
        printf("### ncores: %d\n", c->ncores);
        printf("### npus: %d\n", c->npus);
        fflush(stdout);
    }
    /* for nice output --- not really needed */
    MPI_Barrier(MPI_COMM_WORLD);
    return 0;
}

/**
 * elects some node ranks and distributes them onto all the sockets on the node
 */
static int
bindup_sockets(const context_t *c)
{
    /* if you are going to change bindings often, then cache this */
    if (c->noderank + 1 <= c->nsockets) {
        unsigned socknum = (unsigned)c->noderank;
        if (QUO_SUCCESS != quo_bind_push(c->quo, QUO_SOCKET, socknum)) {
            return 1;
        }
    }
    return 0;
}

/**
 * we can only safely pop bindings that were pushed, so those who were elected
 * to be the socket master can now revert their binding by calling pop.
 */
static int
binddown_sockets(const context_t *c)
{
    if (c->noderank + 1 <= c->nsockets) {
        if (QUO_SUCCESS != quo_bind_pop(c->quo)) {
            return 1;
        }
    }
    return 0;
}

int
main(void)
{
    int erc = EXIT_SUCCESS;
    char *bad_func = NULL;
    context_t *context = NULL;

    /* ////////////////////////////////////////////////////////////////////// */
    /* init code */ 
    /* ////////////////////////////////////////////////////////////////////// */
    if (init(&context)) {
        bad_func = "init";
        goto out;
    }
    /* ////////////////////////////////////////////////////////////////////// */
    /* libquo is now ready for service */ 
    /* ////////////////////////////////////////////////////////////////////// */

    /* first gather some info so we can base our decisions on our current
     * situation. */
    if (sys_grok(context)) {
        bad_func = "sys_grok";
        goto out;
    }
    if (emit_node_basics(context)) {
        bad_func = "emit_node_basics";
        goto out;
    }
    if (emit_bind_state(context)) {
        bad_func = "emit_bind_state";
        goto out;
    }
    if (0 == context->rank) {
        fprintf(stdout, "changing binding...\n");
        fflush(stdout);
    }
    if (bindup_sockets(context)) {
        bad_func = "bindup_sockets";
        goto out;
    }
    if (emit_bind_state(context)) {
        bad_func = "emit_bind_state";
        goto out;
    }
    if (binddown_sockets(context)) {
        bad_func = "bindup_sockets";
        goto out;
    }
    if (0 == context->rank) {
        fprintf(stdout, "reverting binding change...\n");
        fflush(stdout);
    }
    if (emit_bind_state(context)) {
        bad_func = "emit_bind_state";
        goto out;
    }
out:
    if (NULL != bad_func) {
        fprintf(stderr, "XXX %s failure in: %s\n", __FILE__, bad_func);
        erc = EXIT_FAILURE;
    }
    (void)fini(context);
    return erc;
}