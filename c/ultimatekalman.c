/*
 * ultimatekalman.c
 *
 * (C) Sivan Toledo, 2022
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#ifdef _WIN32
// for "unused" attribute
#define __attribute__(x)
#include <float.h>
// string.h for memcpy
#else
#include <unistd.h>
#endif

#define KALMAN_MATRIX_SHORT_TYPE
//#include "ultimatekalman.h"
#include "kalman.h"

#ifdef BUILD_MEX
#include "mex.h"

static char assert_msg[128];
static void mex_assert(int c, int line) {
	if (!c) {
		sprintf(assert_msg,"Assert failed in %s line %d",__FILE__,line);
		mexErrMsgIdAndTxt("MyToolbox:arrayProduct:assertion",assert_msg);
	}
}

#define assert(c) mex_assert((c),__LINE__)
#else
#include <assert.h>
#endif

//static int debug = 0;

/******************************************************************************/
/* UTILITIES                                                                  */
/******************************************************************************/

#define MIN(a,b) ((a)<(b) ? (a) : (b))
#define MAX(a,b) ((a)>(b) ? (a) : (b))

static double NaN = 0.0 / 0.0;

#ifdef MOVED 

#if defined(BUILD_WIN32_GETTIMEOFDAY) && defined(_WIN32)
#include <windows.h>
typedef SSIZE_T ssize_t;

static
int gettimeofday(struct timeval * tp, struct timezone * tzp)
{
    // Note: some broken versions only have 8 trailing zero's, the correct epoch has 9 trailing zero's
    static const uint64_t EPOCH = ((uint64_t) 116444736000000000ULL);

    SYSTEMTIME  system_time;
    FILETIME    file_time;
    uint64_t    time;

    GetSystemTime( &system_time );
    SystemTimeToFileTime( &system_time, &file_time );
    time =  ((uint64_t)file_time.dwLowDateTime )      ;
    time += ((uint64_t)file_time.dwHighDateTime) << 32;

    tp->tv_sec  = (long) ((time - EPOCH) / 10000000L);
    tp->tv_usec = (long) (system_time.wMilliseconds * 1000);
    return 0;
}
#else
#include <sys/time.h>
#endif

#endif

#ifdef MOVED
/******************************************************************************/
/* COVARIANCE MATRICES                                                        */
/******************************************************************************/

matrix_t* cov_weigh(matrix_t* cov, char cov_type, matrix_t* A) {
	blas_int_t M,N,K,LDA,LDB,LDC,NRHS,INFO;
	double ALPHA, BETA;

	assert(A != NULL);
	assert(cov != NULL);

#ifdef BUILD_DEBUG_PRINTOUTS
	printf("cov(%c) ",cov_type);
	matrix_print(cov,"%.3e");
	printf("A ");
	matrix_print(A,"%.3e");
#endif

	matrix_t* WA = NULL;

	int32_t i,j, rows, cols;

	switch (cov_type) {
	case 'W':
		//if (debug) printf("cov W %d %d %d %d\n",matrix_cols(cov),matrix_rows(cov),matrix_cols(A),matrix_rows(A));

		assert(matrix_cols(cov) == matrix_rows(A));

		WA = matrix_create_constant(matrix_rows(A), matrix_cols(A),0.0);

		M = matrix_rows(WA);
		N = matrix_cols(WA);
		K = matrix_cols(cov);
		LDA = matrix_ld(cov);
		LDB = matrix_ld(A);
		LDC = matrix_ld(WA);
		ALPHA = 1.0;
		BETA  = 0.0;
#ifdef BUILD_BLAS_UNDERSCORE
    dgemm_
#else
    dgemm
#endif
		     ("n","N",&M,&N,&K,&ALPHA, cov->elements, &LDA, A->elements, &LDB, &BETA, WA->elements, &LDC
#ifdef BUILD_BLAS_STRLEN_END
          ,1,1
#endif
			 );
		break;
	case 'U': // cov and an upper triangular matrix that we need to solve with
	case 'F': // same representation; in Matlab, we started from explicit cov and factored

		//if (debug) printf("cov U %d %d %d %d\n",matrix_cols(cov),matrix_rows(cov),matrix_cols(A),matrix_rows(A));

		WA = matrix_create_trisolve(cov,A);
		/*
		assert(matrix_cols(cov) == matrix_rows(A));

		WA = matrix_create_copy(A);

		N = matrix_cols(cov);
		LDA = matrix_ld(cov);

		NRHS = matrix_cols(WA);
		LDB = matrix_ld(WA);

		//if (debug) printf("dtrtrs N=%d NRHS=%d LDA=%d LDB=%d\n",N,NRHS,LDA,LDB);
		//if (debug) printf("dtrtrs A %08x state %08x\n",step->Rdiag->elements, state->elements);

		//if (debug) matrix_print(step->Rdiag,NULL);
		//if (debug) matrix_print(kalman->current->state,NULL);

		// upper triangular, no transpose, diagonal is general (not unit)
		dtrtrs("U","N","N", &N, &NRHS, cov->elements, &LDA, WA->elements, &LDB, &INFO);
		*/
		break;
	case 'w':
		assert(matrix_rows(cov) == matrix_rows(A));

		rows = matrix_rows(A);
		cols = matrix_cols(A);

		WA = matrix_create_constant(rows, cols,0.0);

		for (j=0; j<cols; j++) {
			for (i=0; i<rows; i++) {
				matrix_set(WA,i,j, matrix_get(cov,i,0) * matrix_get(A,i,j) );
			}
		}
		break;
	default:
		printf("unknown cov_type %c\n",cov_type);
		assert( 0 );
		WA = matrix_create_constant(matrix_rows(A), matrix_cols(A), NaN);
		break;
	}

	//if (debug) printf("WA ");
	//if (debug) matrix_print(WA,"%.3e");

	return WA;
}

#endif

/******************************************************************************/
/* KALMAN STEPS                                                               */
/******************************************************************************/

typedef struct step_st {
	int64_t step; // logical step number
	int32_t dimension;

	kalman_matrix_t* Rdiag;
	kalman_matrix_t* Rsupdiag;
	kalman_matrix_t* y;

	kalman_matrix_t* Rbar;
	kalman_matrix_t* ybar;

	kalman_matrix_t* state;
	kalman_matrix_t* covariance;
} step_t;

void* step_create() {
	step_t* s = malloc(sizeof(step_t));
	s->step      = -1;
	s->dimension = -1;

	s->Rdiag     = NULL;
	s->Rsupdiag  = NULL;
	s->y         = NULL;

	s->Rbar      = NULL;
	s->ybar      = NULL;

	s->state     = NULL;
	s->covariance= NULL;

	assert( s != NULL );
	return s;
}

//void step_free(step_t* s) {
void step_free(void* v) {
	step_t* s = (step_t*) v;
	matrix_free(s->covariance);
	matrix_free(s->state);
	matrix_free(s->Rdiag);
	matrix_free(s->Rsupdiag);
	matrix_free(s->y);
	matrix_free(s->Rbar);
	matrix_free(s->ybar);
	free(s);
}

int64_t step_get_step(void* v) {
	return ((step_t*) v)->step;
}

int32_t step_get_dimension(void* v) {
	return ((step_t*) v)->dimension;
}

kalman_matrix_t* step_get_state(void* v) {
	return ((step_t*) v)->state;
}

/******************************************************************************/
/* KALMAN                                                                     */
/******************************************************************************/

#ifdef MOVED 

kalman_t* kalman_create() {
	kalman_t* kalman = malloc(sizeof(kalman_t));
	assert( kalman != NULL );
	kalman->steps   = farray_create();
	kalman->current = NULL;
	return kalman;
}

void kalman_free(kalman_t* kalman) {
	//printf("waning: kalman_free not fully implemented yet (steps not processed)\n");

	while (farray_size(kalman->steps) > 0) {
		step_t* i = farray_drop_last(kalman->steps);
		step_free(i);
	}

	farray_free( kalman->steps );
	// step_free( kalman->current );
	free( kalman );
}

int64_t   kalman_earliest(kalman_t* kalman) {
	if ( farray_size(kalman->steps) == 0 ) return -1;
	step_t* s = farray_get_first(kalman->steps);
	return s->step;
}

int64_t   kalman_latest(kalman_t* kalman) {
	if ( farray_size(kalman->steps) == 0 ) return -1;
	step_t* s = farray_get_last(kalman->steps);
	return s->step;
}

#endif // moved

void kalman_evolve(kalman_t* kalman, int32_t n_i, matrix_t* H_i, matrix_t* F_i, matrix_t* c_i, matrix_t* K_i, char K_type) {
	step_t* kalman_current;
	kalman->current = kalman_current = step_create();
	kalman_current->dimension = n_i;

	if (farray_size(kalman->steps)==0) {
		//if (debug) printf("kalman_evolve first step\n");
		kalman_current->step = 0;
		return;
	}

#ifdef BUILD_DEBUG_PRINTOUTS
	printf("kalman_evolve n_i = %d\n",n_i);
	printf("kalman_evolve H_i = %08x %d %d\n",(unsigned int) H_i,(int) matrix_rows(H_i),(int) matrix_cols(H_i));
	printf("kalman_evolve F_i = %08x %d %d\n",(unsigned int) F_i,(int) matrix_rows(F_i),(int) matrix_cols(F_i));
	printf("kalman_evolve i_i = %08x %d %d\n",(unsigned int) c_i,(int) matrix_rows(c_i),(int) matrix_cols(c_i));
	printf("kalman_evolve K_i = %08x %d %d\n",(unsigned int) K_i,(int) matrix_rows(K_i),(int) matrix_cols(K_i));
	printf("cov type %c\n",K_type);
#endif

	step_t* imo = farray_get_last( kalman->steps );
	assert(imo != NULL);
	kalman_current->step = (imo->step) + 1;

	//if (debug) printf("kalman_evolve step = %d\n",kalman->current->step);

	assert(H_i!=NULL);
	assert(F_i!=NULL);
	assert(c_i!=NULL);
	//assert(K_i!=NULL);

	//if (debug) printf("kalman_evolve F_i = %08x %d %d\n",F_i,F_i->row_dim,F_i->col_dim);
	//if (debug) matrix_print(F_i,NULL);

#if 1
	matrix_t* V_i_H_i = cov_weigh(K_i,K_type,H_i);
	matrix_t* V_i_F_i = cov_weigh(K_i,K_type,F_i);
	matrix_t* V_i_c_i = cov_weigh(K_i,K_type,c_i);
#else
	matrix_t* V_i_H_i = matrix_create_copy(H_i);
	matrix_t* V_i_F_i = matrix_create_copy(F_i);
	matrix_t* V_i_c_i = matrix_create_copy(c_i);
#endif

	matrix_mutate_scale(V_i_F_i,-1.0);

#ifdef BUILD_DEBUG_PRINTOUTS
	printf("evolve!\n");
	printf("V_i_H_i ");
	matrix_print(V_i_H_i,"%.3e");
	printf("V_i_F_i ");
	matrix_print(V_i_F_i,"%.3e");
	printf("V_i_c_i ");
	matrix_print(V_i_c_i,"%.3e");
#endif

	matrix_t* A;
	matrix_t* B;
	matrix_t* y;

	if ( imo->Rdiag != NULL ) {
		int32_t z_i = matrix_rows( imo->Rdiag );
		A = matrix_create_vconcat( imo->Rdiag                             , V_i_F_i );
		B = matrix_create_vconcat( matrix_create_constant( z_i, n_i, 0.0 ), V_i_H_i );
		y = matrix_create_vconcat( imo->y                                 , V_i_c_i );
	} else {
		A = matrix_create_copy( V_i_F_i );
		B = matrix_create_copy( V_i_H_i );
		y = matrix_create_copy( V_i_c_i );
	}

#ifdef BUILD_DEBUG_PRINTOUTS
	printf("A ");
	matrix_print(A,"%.3e");
	printf("B ");
	matrix_print(B,"%.3e");
	printf("y ");
	matrix_print(y,"%.3e");
#endif

	/*
	 * QR factorization
	 */

	/*
	blas_int_t M,N,K,LDA,LDC,LWORK,INFO;

	M   = matrix_rows(A);
	N   = matrix_cols(A);
	LDA = matrix_ld (A);

	matrix_t* TAU = matrix_create(M,1);

	double WORK_SCALAR;
	LWORK = -1; // tell lapack to compute the size of the work area required

	//if (debug) printf("dgeqrf: M=%d N=%d LDA=%d LWORK=%d\n",M,N,LDA,LWORK);

	dgeqrf(&M, &N, A->elements, &LDA, TAU->elements, &WORK_SCALAR, &LWORK, &INFO);

	if (INFO != 0) printf("dgeqrf INFO=%d\n",INFO);
	assert(INFO==0);

	LWORK = (int32_t) WORK_SCALAR;
	matrix_t* WORK = matrix_create(LWORK,1);

	//printf("dgeqrf requires %f (%d) words in WORK\n",WORK_SCALAR,LWORK);

//#if 0

	dgeqrf(&M, &N, A->elements, &LDA, TAU->elements, WORK->elements, &LWORK, &INFO);
	if (INFO != 0) printf("dgeqrf INFO=%d\n",INFO);
	assert(INFO==0);

	matrix_free(WORK);

//#endif

	LWORK = -1; // tell lapack to compute the size of the work area required

	M = matrix_rows(B);
	N = matrix_cols(B);
	K = matrix_cols(A); // number of reflectors
	LDC = matrix_ld(B);
	//printf("dormqr M=%d N=%d K=%d LDA=%d LDC=%d\n",M,N,K,LDA,LDC);

	dormqr("L", "T", &M, &N, &K, A->elements, &LDA, TAU->elements, B->elements, &LDC, &WORK_SCALAR, &LWORK, &INFO);
	if (INFO != 0) printf("dormqr INFO=%d\n",INFO);
	assert(INFO==0);

	LWORK = (int32_t) WORK_SCALAR;

	//if (debug) printf("dormqr requires %f (%d) words in WORK\n",WORK_SCALAR,LWORK);

	WORK = matrix_create(LWORK,1);

	// left (pre) multiplication by Q^T
	dormqr("L", "T", &M, &N, &K, A->elements, &LDA, TAU->elements, B->elements, &LDC, WORK->elements, &LWORK, &INFO);
	if (INFO != 0) printf("dormqr INFO=%d\n",INFO);
	assert(INFO==0);

	// we use hte same LWORK since clearly less memory is required now.
	N = matrix_cols(y);
	LDC = matrix_rows(y);
	dormqr("L", "T", &M, &N, &K, A->elements, &LDA, TAU->elements, y->elements, &LDC, WORK->elements, &LWORK, &INFO);
	if (INFO != 0) printf("dormqr INFO=%d\n",INFO);
	assert(INFO==0);


	matrix_free(WORK);

	*/
	matrix_t* TAU = matrix_create_mutate_qr(A);
	matrix_mutate_apply_qt(A,TAU,B);
	matrix_mutate_apply_qt(A,TAU,y);
	matrix_free(TAU);

	//if (debug) matrix_print(A,NULL);
	//if (debug) matrix_print(TAU,NULL);

	/*
	 * These do not require copying either, but we do not want to define references to matrices,
	 * for memory management reasons.
	 */

	int32_t n_imo = imo->dimension;

#ifdef BUILD_DEBUG_PRINTOUTS
	printf("B ");
	matrix_print(B,"%.3e");
	printf("y ");
	matrix_print(y,"%.3e");
#endif

	//printf("%d: A %d %d B %d %d\n",kalman->current->step,matrix_rows(A),matrix_cols(A),matrix_rows(B),matrix_cols(B));

	if (matrix_rows(B) > n_imo) {
		kalman_current->Rbar = matrix_create_sub(B,n_imo,matrix_rows(B)-n_imo, 0, matrix_cols(B));
		kalman_current->ybar = matrix_create_sub(y,n_imo,matrix_rows(y)-n_imo, 0, matrix_cols(y));
	}

	// free imo's Rdiag and y, if they are set
	if (imo->Rdiag != NULL) matrix_free(imo->Rdiag);
	if (imo->y     != NULL) matrix_free(imo->y    );

	matrix_mutate_chop(A,MIN(matrix_rows(A),n_imo),matrix_cols(A)); // we only need the upper triangular part, and that's what we'll use; with the too-long LDA.
	matrix_mutate_chop(B,MIN(matrix_rows(B),n_imo),matrix_cols(B));
	matrix_mutate_chop(y,MIN(matrix_rows(y),n_imo),matrix_cols(y));

	imo->Rdiag    = A;
	imo->Rsupdiag = B;
	imo->y        = y;

	matrix_mutate_triu(imo->Rdiag);

	//matrix_free(y);
	//matrix_free(A);
	//matrix_free(B);


	matrix_free(V_i_c_i);
	matrix_free(V_i_F_i);
	matrix_free(V_i_H_i);
}

void kalman_observe(kalman_t* kalman, matrix_t* G_i, matrix_t* o_i, matrix_t* C_i, char C_type) {

#ifdef BUILD_DEBUG_PRINTOUTS
	printf("observe!\n");
#endif

	assert(kalman != NULL);
	assert(kalman->current != NULL);
	step_t* kalman_current = kalman->current;
	int32_t n_i = kalman_current->dimension;

#ifdef BUILD_DEBUG_PRINTOUTS
	printf("observe %d\n",(int) kalman_current->step);
#endif

	matrix_t* W_i_G_i = NULL;
	matrix_t* W_i_o_i = NULL;

	if (o_i != NULL) { // no observations
#ifdef BUILD_DEBUG_PRINTOUTS
		printf("C_i(%c) ",C_type);
		matrix_print(C_i,"%.3e");
		printf("G_i ");
		matrix_print(G_i,"%.3e");
		printf("o_i ");
		matrix_print(o_i,"%.3e");
#endif

#if 1
		W_i_G_i = cov_weigh(C_i,C_type,G_i);
		W_i_o_i = cov_weigh(C_i,C_type,o_i);
#else
		W_i_G_i = matrix_create_copy(G_i);
		W_i_o_i = matrix_create_copy(o_i);
#endif

#ifdef BUILD_DEBUG_PRINTOUTS
		printf("W_i_G_i ");
		matrix_print(W_i_G_i,"%.3e");
		printf("W_i_o_i ");
		matrix_print(W_i_o_i,"%.3e");
#endif
	}

	//if (debug) printf("kalman_observe %08x %d %08x %d\n",G_i,G_i?matrix_rows(G_i):0, kalman->current->Rbar, kalman->current->Rbar?matrix_rows(kalman->current->Rbar):0);
	//if (debug) printf("current ybar %08x Rbar %08x Woi %08x\n",kalman->current->Rbar, kalman->current->ybar, W_i_o_i);

	matrix_t* A = matrix_create_vconcat(kalman_current->Rbar, W_i_G_i);
	matrix_t* y = matrix_create_vconcat(kalman_current->ybar, W_i_o_i);

	if ( A != NULL ) { // we got some rows from at least one of the two blocks
#ifdef BUILD_DEBUG_PRINTOUTS
		printf("A ");
		matrix_print(A,"%.3e");
		printf("y ");
		matrix_print(y,"%.3e");
#endif

		if (matrix_rows(A) >= matrix_cols(A)) {
		//printf("%d: A %d %d obs? %d Rbar? %d\n",kalman->current->step,matrix_rows(A),matrix_cols(A),o_i!=NULL, kalman->current->Rbar!=NULL);

		/*
		 * QR factorization
		 */
			/*
		blas_int_t M,N,K,LDA,LDC,LWORK,INFO;

		M   = matrix_rows(A);
		N   = matrix_cols(A);
		LDA = matrix_ld  (A);

		matrix_t* TAU = matrix_create(M,1);

		double WORK_SCALAR;
		LWORK = -1; // tell lapack to compute the size of the work area required

		//if (debug) printf("dgeqrf: M=%d N=%d LDA=%d LWORK=%d\n",M,N,LDA,LWORK);

		dgeqrf(&M, &N, A->elements, &LDA, TAU->elements, &WORK_SCALAR, &LWORK, &INFO);
		if (INFO != 0) printf("dgeqrf INFO=%d\n",INFO);
		assert(INFO==0);

		LWORK = (int32_t) WORK_SCALAR;
		matrix_t* WORK = matrix_create(LWORK,1);

		//if (debug) printf("dgeqrf requires %f (%d) words in WORK\n",WORK_SCALAR,LWORK);

		dgeqrf(&M, &N, A->elements, &LDA, TAU->elements, WORK->elements, &LWORK, &INFO);
		if (INFO != 0) printf("dgeqrf INFO=%d\n",INFO);
		assert(INFO==0);

		matrix_free(WORK);

		LWORK = -1; // tell lapack to compute the size of the work area required

		M = matrix_rows(y);
		N = matrix_cols(y);
		K = matrix_cols(A); // number of reflectors
		LDC = matrix_ld(y);
		//if (debug) printf("dormqr M=%d N=%d K=%d LDA=%d LDC=%d\n",M,N,K,LDA,LDC);
		if (debug) printf("%d: dormqr M=%d N=%d K=%d LDA=%d LDC=%d\n",kalman->current->step,M,N,K,LDA,LDC);

		dormqr("L", "T", &M, &N, &K, A->elements, &LDA, TAU->elements, y->elements, &LDC, &WORK_SCALAR, &LWORK, &INFO);
		if (INFO != 0) printf("dormqr INFO=%d\n",INFO);
		assert(INFO==0);

		LWORK = (int32_t) WORK_SCALAR;

		//if (debug) printf("dormqr requires %f (%d) words in WORK\n",WORK_SCALAR,LWORK);

		WORK = matrix_create(LWORK,1);

		// left (pre) multiplication by Q^T
		dormqr("L", "T", &M, &N, &K, A->elements, &LDA, TAU->elements, y->elements, &LDC, WORK->elements, &LWORK, &INFO);
		if (INFO != 0) printf("dormqr INFO=%d\n",INFO);
		assert(INFO==0);

		matrix_free(WORK);
		matrix_free(TAU);
		*/

			matrix_t* TAU = matrix_create_mutate_qr(A);
			matrix_mutate_apply_qt(A,TAU,y);
			matrix_free(TAU);


		/*********************************************/

		int32_t n_i = kalman_current->dimension;

		//if (debug) printf("observe n_i = %d\n",n_i);

		//if (debug) printf("**** %d %d %d %d\n",matrix_rows(A),matrix_ld(A),matrix_rows(y),matrix_ld(y));

#ifdef BUILD_DEBUG_PRINTOUTS
		printf("A ");
		matrix_print(A,"%.3e");
		printf("y ");
		matrix_print(y,"%.3e");
#endif

		matrix_mutate_chop(A,MIN(matrix_rows(A),n_i),matrix_cols(A));
		matrix_mutate_chop(y,MIN(matrix_rows(y),n_i),matrix_cols(y));

		//if (debug) printf("**** %d %d %d %d\n",matrix_rows(A),matrix_ld(A),matrix_rows(y),matrix_ld(y));

#ifdef BUILD_DEBUG_PRINTOUTS
		printf("A ");
		matrix_print(A,"%.3e");
#endif

		kalman_current->Rdiag  = A;
		kalman_current->y      = y;

		matrix_mutate_triu(kalman_current->Rdiag);

		} else { // A is flat, no need to factor
			//printf("obs step %d no need to factor, flat\n",kalman->current->step);
			kalman_current->Rdiag  = A;
			kalman_current->y      = y;
		}

		// solve for the estimate

		matrix_t* state = NULL;

		if (matrix_rows(kalman_current->Rdiag)==n_i) {

			state = matrix_create_trisolve(kalman_current->Rdiag,y);

			/*

			state = matrix_create_copy(y);

			//if (debug) printf("**** %d %d %d %d\n",matrix_rows(A),matrix_ld(A),matrix_rows(kalman->current->state),matrix_ld(kalman->current->state));

			blas_int_t N,LDA,NRHS,LDB,INFO;
			N = matrix_cols(kalman->current->Rdiag);
			LDA = matrix_ld(kalman->current->Rdiag);

			NRHS = matrix_cols(state);
			LDB = matrix_ld(state);

			if (debug) printf("dtrtrs N=%d NRHS=%d LDA=%d LDB=%d\n",N,NRHS,LDA,LDB);

			if (debug) matrix_print(kalman->current->Rdiag,NULL);
			if (debug) matrix_print(state,NULL);

			// upper triangular, no transpose, diagonal is general (not unit)
			dtrtrs("U","N","N", &N, &NRHS, kalman->current->Rdiag->elements, &LDA, state->elements, &LDB, &INFO);
			if (INFO != 0) printf("dormqr INFO=%d\n",INFO);
			assert(INFO==0);
			if (debug) printf("dtrtr done\n");
			*/
		} else {
			state = matrix_create_constant(n_i,1,NaN);
		}

		kalman_current->state = state;
		kalman_current->covariance = matrix_create_copy(kalman_current->Rdiag);
	}

	matrix_free(W_i_G_i);
	matrix_free(W_i_o_i);

	farray_append(kalman->steps,kalman->current);
	//kalman->current = NULL; // had no effect, commented out Aug 2024 to avoid bugs in testing

#ifdef BUILD_DEBUG_PRINTOUTS
	printf("kalman_observe done\n");
#endif
}

#ifdef MOVED

matrix_t* kalman_estimate(kalman_t* kalman, int64_t si) {
#ifdef BUILD_DEBUG_PRINTOUTS
	printf("kalman_estimate\n");
#endif

	if (farray_size(kalman->steps) == 0) return NULL;

	if (si < 0) si = farray_last_index(kalman->steps);
	step_t* step = farray_get(kalman->steps,si);

	if (step->state == NULL) return matrix_create_constant(step->dimension,1,NaN);

	return matrix_create_copy(step->state);
#if 0
	int32_t n_i = step->dimension;

	matrix_t* state = NULL;

	if (step->Rdiag != NULL && step->y != NULL && matrix_rows(step->Rdiag)==n_i) {
		state = matrix_create_copy(step->y);

		//if (debug) printf("**** %d %d %d %d\n",matrix_rows(A),matrix_ld(A),matrix_rows(kalman->current->state),matrix_ld(kalman->current->state));

		blas_int_t N,LDA,NRHS,LDB,INFO;
		N = matrix_cols(step->Rdiag);
		LDA = matrix_ld(step->Rdiag);

		NRHS = matrix_cols(state);
		LDB = matrix_ld(state);

#ifdef BUILD_DEBUG_PRINTOUTS
		printf("dtrtrs N=%d NRHS=%d LDA=%d LDB=%d\n",N,NRHS,LDA,LDB);
		printf("dtrtrs A %08x state %08x\n",step->Rdiag->elements, state->elements);
#endif
		//if (debug) matrix_print(step->Rdiag,NULL);
		//if (debug) matrix_print(kalman->current->state,NULL);

		// upper triangular, no transpose, diagonal is general (not unit)
#ifdef BUILD_BLAS_UNDERSCORE
     dtrtrs_
#else
     dtrtrs
#endif
		       ("U","N","N", &N, &NRHS, step->Rdiag->elements, &LDA, state->elements, &LDB, &INFO);
#ifdef BUILD_BLAS_STRLEN_END
    ,1,1,1
#endif
    );
		if (INFO != 0) {
#ifdef BUILD_DEBUG_PRINTOUTS
			printf("dormqr INFO=%d\n",INFO);
#endif
			}
		assert(INFO==0);
	} else {
		state = matrix_create_constant(n_i,1,NaN);
	}

	return state;
#endif
}

#endif // moved

void kalman_smooth(kalman_t* kalman) {
	//printf("forget %d\n",si);
	if (farray_size(kalman->steps) == 0) return;

	int64_t si;
	int64_t last  = farray_last_index (kalman->steps);
	int64_t first = farray_first_index(kalman->steps);

	step_t* i;

	//printf("smooth1 %d to %d\n",last,first);

	matrix_t* prev_state;
	for (si=last; si>=first; si--) {
		i = farray_get(kalman->steps,si);
		//printf("smth %d: %08x %08x %08x\n",si,i->Rdiag,i->Rsupdiag,i->y);
		//if (i->Rdiag !=NULL) printf("smth %d: Rdiag %d X %d\n",si,matrix_rows(i->Rdiag),matrix_cols(i->Rdiag));
		if (i->state == NULL) { // not clear why this would happen,  but it happens in projectile
			i->state = matrix_create(matrix_rows(i->y),1);
		}
		assert(i->state != NULL);
		//printf("smth %d: %d <- %d\n",si,matrix_rows(i->state),matrix_rows(i->y));
		matrix_mutate_copy(i->state,i->y);
		if (si < last) {
			matrix_mutate_gemm(-1.0,i->Rsupdiag,prev_state,1.0,i->state);
		}
		//printf("smth %d: %d X %d ; %d\n",si,matrix_rows(i->Rdiag),matrix_cols(i->Rdiag),matrix_rows(i->state));
		matrix_mutate_trisolve(i->Rdiag,i->state);
		prev_state = i->state;
	}

	//printf("smooth2 %d to %d\n",last,first);

#ifndef NO_COVARIANCE_ESTIMATES
	matrix_t* R;
	int32_t n_i, n_ipo;
	for (si=last; si>=first; si--) {
		i = farray_get(kalman->steps,si);
		if (si == last) {
			R = i->Rdiag;
			n_ipo = matrix_rows(i->Rdiag);
		} else {
			matrix_free(i->covariance);

			n_i = matrix_rows(i->Rdiag);
			matrix_t* A = matrix_create_vconcat(i->Rsupdiag,R);
			matrix_t* S = matrix_create_vconcat(i->Rdiag,matrix_create_constant(matrix_rows(R),matrix_cols(i->Rdiag),0.0));
			matrix_t* TAU = matrix_create_mutate_qr(A);
			matrix_mutate_apply_qt(A,TAU,S);
			matrix_free(TAU);
			matrix_free(A);

			R = i->covariance = matrix_create_sub(S,n_ipo,n_i,0,n_i);
			matrix_free(S);

			n_ipo = n_i;
		}
	}
#endif
}

char kalman_covariance_type(kalman_t* kalman, int64_t si) { return 'W'; }

matrix_t* kalman_covariance(kalman_t* kalman, int64_t si) {
	//int32_t i,j;

#ifdef BUILD_DEBUG_PRINTOUTS
	printf("kalman_covariance\n");
#endif

	if (farray_size(kalman->steps) == 0) return NULL;

	if (si < 0) si = farray_last_index(kalman->steps);
	step_t* step = farray_get(kalman->steps,si);

	int32_t n_i = step->dimension;

	matrix_t* cov = NULL;

	if (step->Rdiag != NULL && matrix_rows(step->Rdiag)==n_i) {
		cov = matrix_create_copy(step->covariance);

		//printf("cov ");
		//matrix_print(cov,"%.3e");

		//for (j=0; j<n_i; j++) {
		//	for (i=j+1; i<n_i; i++) {
		//		matrix_set(cov,i,j,0.0);
		//	}
		//}
	} else {
		cov = matrix_create_constant(n_i,n_i,NaN);
	}

	return cov;
}

#ifdef MOVED

void kalman_forget(kalman_t* kalman, int64_t si) {
#ifdef BUILD_DEBUG_PRINTOUTS
	printf("forget %d\n",(int) si);
#endif

	if (farray_size(kalman->steps) == 0) return;

	if (si < 0) si = farray_last_index(kalman->steps) - 1;

#ifdef BUILD_DEBUG_PRINTOUTS
	printf("forget %d now %d to %d\n",(int) si,(int) farray_first_index(kalman->steps),(int) farray_last_index(kalman->steps));
#endif

	if (si > farray_last_index (kalman->steps) - 1) return; // don't delete last step
	if (si < farray_first_index(kalman->steps)    ) return; // nothing to delete


	while (farray_first_index(kalman->steps) <= si) {
		step_t* step = farray_drop_first(kalman->steps);
		step_free(step);
#ifdef BUILD_DEBUG_PRINTOUTS
		printf("forget new first %d\n",(int) farray_first_index(kalman->steps));
#endif
	}
#ifdef BUILD_DEBUG_PRINTOUTS
	printf("forget %d done\n",(int) si);
#endif
}

#endif // moved

void kalman_rollback(kalman_t* kalman, int64_t si) {
	//printf("rollback %d\n",si);
	if (farray_size(kalman->steps) == 0) return;

	if (si > farray_last_index (kalman->steps)) return; // we can roll  back even the last step (its observation)
	if (si < farray_first_index(kalman->steps)) return;

	step_t* step;
	do {
		step = farray_drop_last(kalman->steps);
		if (step->step == si) {
			//printf("rollback got to %d\n",si);

			/*
			kalman->current = step_create();
			kalman->current->dimension = step->dimension;
			kalman->current->step      = step->step;
			kalman->current->Rbar      = step->Rbar;
			kalman->current->ybar      = step->ybar;
			 */
			// TODO free...

			matrix_free( step->Rdiag     );
			matrix_free( step->Rsupdiag  );
			matrix_free( step->y         );
			matrix_free( step->state     );
			matrix_free( step->covariance);
			kalman->current = step;

		} else {
			//printf("rollback dropped step %d\n",step->step);
		}
	} while (step->step > si);

	//printf("rollback to %d new latest %d\n",si,kalman_latest(kalman));

}

#ifdef MOVED 

static struct timeval begin, end;

matrix_t* kalman_perftest(kalman_t* kalman,
		                      matrix_t* H, matrix_t* F, matrix_t* c, matrix_t* K, char K_type,
		                      matrix_t* G, matrix_t* o,              matrix_t* C, char C_type,
									        int32_t count, int32_t decimation) {

	matrix_t* t = matrix_create_constant(count/decimation, 1, NaN);
	int32_t i,j,n;

	//printf("perftest count %d decimation %d rows %d\n",count,decimation,matrix_rows(t));

	//struct timeval begin, end;
	gettimeofday(&begin, 0);

	j = 0;
	n = matrix_cols(G);

	for (i=0; i<count; i++) {
		//printf("perftest iter %d (j=%d)\n",i,j);
		//if (debug) printf("perftest iter %d (j=%d)\n",i,j);
		kalman_evolve(kalman,n,H,F,c,K,K_type);
		kalman_observe(kalman,G,o,C,C_type);
		matrix_t* e = kalman_estimate(kalman,-1);
		matrix_free(e);
		kalman_forget(kalman,-1);

		if ((i % decimation) == decimation-1) {
			gettimeofday(&end, 0);
			long seconds      = end.tv_sec  - begin.tv_sec;
			long microseconds = end.tv_usec - begin.tv_usec;
			double elapsed    = seconds + microseconds*1e-6;

			assert(j < matrix_rows(t));

			//printf("perftest store i=%d (j=%d) %.3e\n",i,j,elapsed/decimation);
			matrix_set(t,j,0,elapsed / decimation); // average per step
			j = j+1;

			gettimeofday(&begin, 0);
			//begin = end;
		}
	}

	//printf("perftest done\n");
	//printf("  matrix_t* t = %08x\n",t);

	//printf("perftest count %d decimation %d rows %d cols %d\n",count,decimation,matrix_rows(t),matrix_cols(t));

	//matrix_print(t,"%.3e");

	return t;
}

#endif // moved

/******************************************************************************/
/* MAIN                                                                       */
/******************************************************************************/

#if 0

int main(int argc, char *argv[]) {
  printf("hello world\n");

#if 0
  matrix_t* A = matrix_create(7,8);
  matrix_t* I = matrix_create_identity(7,8);
  matrix_set(I,6,7,13.0);
  if (debug) matrix_print(I,NULL,NULL);
  //matrix_set(I,7,0,13.0);
#endif

  farray_t* a = farray_create();
  printf("farray size %lld\n",farray_size(a));

  farray_append(a,(void*) 0);
  farray_append(a,(void*) 1);
  farray_append(a,(void*) 2);
  farray_append(a,(void*) 3);

  printf("farray size %lld\n",farray_size(a));
  printf("farray first %lld\n",farray_first_index(a));
  printf("farray last  %lld\n",farray_last_index(a));
  for (int i=farray_first_index(a); i<=farray_last_index(a); i++) {
  	printf("farray get(%d) %lld\n",i,(int64_t) farray_get(a,i));
  }

  farray_drop_first(a);
  farray_drop_first(a);

  printf("farray size %lld\n",farray_size(a));
  printf("farray first %lld\n",farray_first_index(a));
  printf("farray last  %lld\n",farray_last_index(a));
  for (int i=farray_first_index(a); i<=farray_last_index(a); i++) {
  	printf("farray get(%d) %lld\n",i,(int64_t) farray_get(a,i));
  }

	printf("farray get_first %lld\n",(int64_t) farray_get_first(a));
	printf("farray get_last  %lld\n",(int64_t) farray_get_last(a));

  printf("  farray append 10\n");
  for (int i=0; i<10; i++) farray_append(a,(void*)77);

  printf("farray size %lld\n",farray_size(a));
  printf("farray first %lld\n",farray_first_index(a));
  printf("farray last  %lld\n",farray_last_index(a));

  printf("  farray drop 9\n");
  for (int i=0; i<9; i++) farray_drop_first(a);

  printf("farray size %lld\n",farray_size(a));
  printf("farray first %lld\n",farray_first_index(a));
  printf("farray last  %lld\n",farray_last_index(a));

  printf("  farray append 128\n");
  for (int i=0; i<128; i++) farray_append(a,(void*)77);

  printf("farray size %lld\n",farray_size(a));
  printf("farray first %lld\n",farray_first_index(a));
  printf("farray last  %lld\n",farray_last_index(a));

  double A[] = {
    1, 2, 3,
  	4, 5, 6
  };

  matrix_t* W = matrix_create_from_rowwise(A,2,3);
  if (debug) matrix_print(W,NULL,NULL);
}
#endif

/******************************************************************************/
/* END OF FILE                                                                */
/******************************************************************************/
