/*
  Copyright 2019 Equinor ASA

  This file is part of the Open Porous Media project (OPM).

  OPM is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  OPM is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with OPM.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef __NVCC__
	#error "Cannot compile for cusparse: NVIDIA compiler not found"
#endif

#include <cstdio>
#include <cstdlib>
#include <cuda_runtime.h>
#include <iostream>
#include <sys/time.h>
#include <sstream>

#include <opm/common/OpmLog/OpmLog.hpp>

#include <opm/simulators/linalg/bda/cusparseSolverBackend.hpp>
#include <opm/simulators/linalg/bda/BdaResult.hpp>
#include <opm/simulators/linalg/bda/cuda_header.h>

#include "cublas_v2.h"
#include "cusparse_v2.h"
// For more information about cusparse, check https://docs.nvidia.com/cuda/cusparse/index.html

namespace Opm
{

	const cusparseSolvePolicy_t policy = CUSPARSE_SOLVE_POLICY_USE_LEVEL;
	const cusparseOperation_t operation  = CUSPARSE_OPERATION_NON_TRANSPOSE;
	const cusparseDirection_t order = CUSPARSE_DIRECTION_ROW;

	double second(void){
		struct timeval tv;
		gettimeofday(&tv, nullptr);
		return (double)tv.tv_sec + (double)tv.tv_usec / 1000000.0;
	}

	cusparseSolverBackend::cusparseSolverBackend(int verbosity_, int maxit_, double tolerance_) : verbosity(verbosity_), maxit(maxit_), tolerance(tolerance_), minit(0){
	}

	cusparseSolverBackend::~cusparseSolverBackend(){
		finalize();
	}

	void cusparseSolverBackend::gpu_pbicgstab(BdaResult& res){
		double t_total1, t_total2;
		int n = N;
		double rho = 1.0, rhop;
		double alpha, nalpha, beta;
		double omega, nomega, tmp1, tmp2;
		double norm, norm_0;
		double zero = 0.0;
		double one  = 1.0;
		double mone = -1.0;
		float it;

		t_total1 = second();

		cusparseDbsrmv(cusparseHandle, order, operation, Nb, Nb, nnzb, &one, descr_M, d_bVals, d_bRows, d_bCols, BLOCK_SIZE, d_x, &zero, d_r);

		cublasDscal(cublasHandle, n, &mone, d_r, 1);
		cublasDaxpy(cublasHandle, n, &one, d_b, 1, d_r, 1);
		cublasDcopy(cublasHandle, n, d_r, 1, d_rw, 1);
		cublasDcopy(cublasHandle, n, d_r, 1, d_p, 1); 
		cublasDnrm2(cublasHandle, n, d_r, 1, &norm_0);

		if(verbosity > 1){
			std::ostringstream out;
			out << std::scientific << "cusparseSolver initial norm: " << norm_0;
			OpmLog::info(out.str());
		}

		for(it = 0.5; it < maxit; it+=0.5){
			rhop = rho;
			cublasDdot(cublasHandle, n, d_rw, 1, d_r, 1, &rho);

			if(it > 1){
				beta = (rho/rhop) * (alpha/omega);
				nomega = -omega;
				cublasDaxpy(cublasHandle, n, &nomega, d_v, 1, d_p, 1);
				cublasDscal(cublasHandle, n, &beta, d_p, 1);
				cublasDaxpy(cublasHandle, n, &one, d_r, 1, d_p, 1);
			}

			// apply ilu0
			cusparseDbsrsv2_solve(cusparseHandle, order, \
				operation, Nb, nnzb, &one, \
				descr_L, d_mVals, d_mRows, d_mCols, BLOCK_SIZE, info_L, d_p, d_t, policy, d_buffer);
			cusparseDbsrsv2_solve(cusparseHandle, order, \
				operation, Nb, nnzb, &one, \
				descr_U, d_mVals, d_mRows, d_mCols, BLOCK_SIZE, info_U, d_t, d_pw, policy, d_buffer);

			// spmv
			cusparseDbsrmv(cusparseHandle, order, \
				operation, Nb, Nb, nnzb, \
				&one, descr_M, d_bVals, d_bRows, d_bCols, BLOCK_SIZE, d_pw, &zero, d_v);

			cublasDdot(cublasHandle, n, d_rw, 1, d_v, 1, &tmp1);
			alpha = rho / tmp1;
			nalpha = -alpha;
			cublasDaxpy(cublasHandle, n, &nalpha, d_v, 1, d_r, 1);
			cublasDaxpy(cublasHandle, n, &alpha, d_pw, 1, d_x, 1);
			cublasDnrm2(cublasHandle, n, d_r, 1, &norm);

			if(norm < tolerance * norm_0 && it > minit){
				break;
			}

			it += 0.5;

			// apply ilu0
			cusparseDbsrsv2_solve(cusparseHandle, order, \
				operation, Nb, nnzb, &one, \
				descr_L, d_mVals, d_mRows, d_mCols, BLOCK_SIZE, info_L, d_r, d_t, policy, d_buffer);
			cusparseDbsrsv2_solve(cusparseHandle, order, \
				operation, Nb, nnzb, &one, \
				descr_U, d_mVals, d_mRows, d_mCols, BLOCK_SIZE, info_U, d_t, d_s, policy, d_buffer);

			// spmv
			cusparseDbsrmv(cusparseHandle, order, \
				operation, Nb, Nb, nnzb, &one, descr_M, \
				d_bVals, d_bRows, d_bCols, BLOCK_SIZE, d_s, &zero, d_t);

			cublasDdot(cublasHandle, n, d_t, 1, d_r, 1, &tmp1);
			cublasDdot(cublasHandle, n, d_t, 1, d_t, 1, &tmp2);
			omega = tmp1 / tmp2;
			nomega = -omega;
			cublasDaxpy(cublasHandle, n, &omega, d_s, 1, d_x, 1);
			cublasDaxpy(cublasHandle, n, &nomega, d_t, 1, d_r, 1);

			cublasDnrm2(cublasHandle, n, d_r, 1, &norm);


			if(norm < tolerance * norm_0 && it > minit){
				break;
			}

			if(verbosity > 1){
				std::ostringstream out;
				out << "it: " << it << std::scientific << ", norm: " << norm;
				OpmLog::info(out.str());
			}
		}

		t_total2 = second();

		res.iterations = std::min(it, (float)maxit);
		res.reduction = norm/norm_0;
		res.conv_rate  = static_cast<double>(pow(res.reduction,1.0/it));
		res.elapsed = t_total2 - t_total1;
		res.converged = (it != (maxit + 0.5));

		if(verbosity > 0){
			std::ostringstream out;
			out << "=== converged: " << res.converged << ", conv_rate: " << res.conv_rate << ", time: " << res.elapsed << \
			       ", time per iteration: " << res.elapsed/it << ", iterations: " << it;
			OpmLog::info(out.str());
		}
	}


	void cusparseSolverBackend::initialize(int N, int nnz, int dim){
		this->N = N;
		this->nnz = nnz;
		this->BLOCK_SIZE = dim;
		this->nnzb = nnz/BLOCK_SIZE/BLOCK_SIZE;
		Nb = (N + dim - 1) / dim;
		std::ostringstream out;
		out << "Initializing GPU, matrix size: " << N << " blocks, nnz: " << nnzb << " blocks";
		OpmLog::info(out.str());
		out.str("");
		out.clear();
		out << "Minit: " << minit << ", maxit: " << maxit << std::scientific << ", tolerance: " << tolerance;
		OpmLog::info(out.str());

		int deviceID = 0;
		cudaSetDevice(deviceID);
		cudaCheckLastError("Could not get device");
		struct cudaDeviceProp props;
		cudaGetDeviceProperties(&props, deviceID);
		cudaCheckLastError("Could not get device properties");
		out.str("");
		out.clear();
		out << "Name GPU: " << props.name << ", Compute Capability: " << props.major << "." << props.minor;
		OpmLog::info(out.str());

		cudaStreamCreate(&stream);
		cudaCheckLastError("Could not create stream");

		cublasCreate(&cublasHandle);
		cudaCheckLastError("Could not create cublasHandle");

		cusparseCreate(&cusparseHandle);
		cudaCheckLastError("Could not create cusparseHandle");

		cudaMalloc((void**)&d_x, sizeof(double) * N);
		cudaMalloc((void**)&d_b, sizeof(double) * N);
		cudaMalloc((void**)&d_r, sizeof(double) * N);
		cudaMalloc((void**)&d_rw,sizeof(double) * N);
		cudaMalloc((void**)&d_p, sizeof(double) * N);
		cudaMalloc((void**)&d_pw,sizeof(double) * N);
		cudaMalloc((void**)&d_s, sizeof(double) * N);
		cudaMalloc((void**)&d_t, sizeof(double) * N);
		cudaMalloc((void**)&d_v, sizeof(double) * N);
		cudaMalloc((void**)&d_bVals, sizeof(double) * nnz);
		cudaMalloc((void**)&d_bCols, sizeof(double) * nnz);
		cudaMalloc((void**)&d_bRows, sizeof(double) * (Nb+1));
		cudaMalloc((void**)&d_mVals, sizeof(double) * nnz);
		cudaCheckLastError("Could not allocate enough memory on GPU");

		cublasSetStream(cublasHandle, stream);
		cudaCheckLastError("Could not set stream to cublas");
		cusparseSetStream(cusparseHandle, stream);
		cudaCheckLastError("Could not set stream to cusparse");

		cudaMallocHost((void**)&x, sizeof(double) * N);
		cudaCheckLastError("Could not allocate pinned host memory");

		initialized = true;
	} // end initialize()

	void cusparseSolverBackend::finalize(){
		cudaFree(d_x);
		cudaFree(d_b);
		cudaFree(d_r);
		cudaFree(d_rw);
		cudaFree(d_p);
		cudaFree(d_pw);
		cudaFree(d_s);
		cudaFree(d_t);
		cudaFree(d_v);
		cudaFree(d_mVals);
		cudaFree(d_bVals);
		cudaFree(d_bCols);
		cudaFree(d_bRows);
		cudaFree(d_buffer);
		cusparseDestroyBsrilu02Info(info_M);
		cusparseDestroyBsrsv2Info(info_L);
		cusparseDestroyBsrsv2Info(info_U);
		cusparseDestroyMatDescr(descr_B);
		cusparseDestroyMatDescr(descr_M);
		cusparseDestroyMatDescr(descr_L);
		cusparseDestroyMatDescr(descr_U);
		cusparseDestroy(cusparseHandle);
		cublasDestroy(cublasHandle);
		cudaHostUnregister(vals);
		cudaHostUnregister(cols);
		cudaHostUnregister(rows);
		cudaStreamDestroy(stream);
		cudaFreeHost(x);
	} // end finalize()


	void cusparseSolverBackend::copy_system_to_gpu(double *vals, int *rows, int *cols, double *b){

		double t1, t2;
		if(verbosity > 2){
			t1 = second();
		}

		// information cudaHostRegister: https://docs.nvidia.com/cuda/cuda-runtime-api/group__CUDART__MEMORY.html#group__CUDART__MEMORY_1ge8d5c17670f16ac4fc8fcb4181cb490c
		// possible flags for cudaHostRegister: cudaHostRegisterDefault, cudaHostRegisterPortable, cudaHostRegisterMapped, cudaHostRegisterIoMemory
		cudaHostRegister(vals, nnz * sizeof(double), cudaHostRegisterDefault);
		cudaHostRegister(cols, nnz * sizeof(int), cudaHostRegisterDefault);
		cudaHostRegister(rows, (Nb+1) * sizeof(int), cudaHostRegisterDefault);
		cudaHostRegister(b, N * sizeof(double), cudaHostRegisterDefault);
		cudaMemcpyAsync(d_bVals, vals, nnz * sizeof(double), cudaMemcpyHostToDevice, stream);
		cudaMemcpyAsync(d_bCols, cols, nnz * sizeof(int), cudaMemcpyHostToDevice, stream);
		cudaMemcpyAsync(d_bRows, rows, (Nb+1) * sizeof(int), cudaMemcpyHostToDevice, stream);
		cudaMemcpyAsync(d_b, b, N * sizeof(double), cudaMemcpyHostToDevice, stream);
		cudaMemsetAsync(d_x, 0, sizeof(double) * N, stream);

		this->vals = vals;
		this->cols = cols;
		this->rows = rows;

		if(verbosity > 2){
			cudaStreamSynchronize(stream);
			t2 = second();
			std::ostringstream out;
			out << "cusparseSolver::copy_system_to_gpu(): " << t2-t1 << " s";
			OpmLog::info(out.str());
		}
	} // end copy_system_to_gpu()


	// don't copy rowpointers and colindices, they stay the same
	void cusparseSolverBackend::update_system_on_gpu(double *vals, double *b){

		double t1, t2;
		if(verbosity > 2){
			t1 = second();
		}

		cudaMemcpyAsync(d_bVals, vals, nnz * sizeof(double), cudaMemcpyHostToDevice, stream);
		cudaMemcpyAsync(d_b, b, N * sizeof(double), cudaMemcpyHostToDevice, stream);
		cudaMemsetAsync(d_x, 0, sizeof(double) * N, stream);

		if(verbosity > 2){
			cudaStreamSynchronize(stream);
			t2 = second();
			std::ostringstream out;
			out << "cusparseSolver::update_system_on_gpu(): " << t2-t1 << " s";
			OpmLog::info(out.str());
		}
	} // end update_system_on_gpu()


	void cusparseSolverBackend::reset_prec_on_gpu(){
		cudaMemcpyAsync(d_mVals, d_bVals, nnz  * sizeof(double), cudaMemcpyDeviceToDevice, stream);
	}


	bool cusparseSolverBackend::analyse_matrix(){

		int d_bufferSize_M, d_bufferSize_L, d_bufferSize_U, d_bufferSize;
		double t1, t2;

		if(verbosity > 2){
			t1 = second();
		}

		cusparseCreateMatDescr(&descr_B);
		cusparseCreateMatDescr(&descr_M);
		cusparseSetMatType(descr_B, CUSPARSE_MATRIX_TYPE_GENERAL);
		cusparseSetMatType(descr_M, CUSPARSE_MATRIX_TYPE_GENERAL);
		const cusparseIndexBase_t base_type = CUSPARSE_INDEX_BASE_ZERO;		// matrices from Flow are base0

		cusparseSetMatIndexBase(descr_B, base_type);
		cusparseSetMatIndexBase(descr_M, base_type);

		cusparseCreateMatDescr(&descr_L);
		cusparseSetMatIndexBase(descr_L, base_type);
		cusparseSetMatType(descr_L, CUSPARSE_MATRIX_TYPE_GENERAL);
		cusparseSetMatFillMode(descr_L, CUSPARSE_FILL_MODE_LOWER);
		cusparseSetMatDiagType(descr_L, CUSPARSE_DIAG_TYPE_UNIT);

		cusparseCreateMatDescr(&descr_U);
		cusparseSetMatIndexBase(descr_U, base_type);
		cusparseSetMatType(descr_U, CUSPARSE_MATRIX_TYPE_GENERAL);
		cusparseSetMatFillMode(descr_U, CUSPARSE_FILL_MODE_UPPER);
		cusparseSetMatDiagType(descr_U, CUSPARSE_DIAG_TYPE_NON_UNIT);
		cudaCheckLastError("Could not initialize matrix descriptions");

		cusparseCreateBsrilu02Info(&info_M);
		cusparseCreateBsrsv2Info(&info_L);
		cusparseCreateBsrsv2Info(&info_U);
		cudaCheckLastError("Could not create analysis info");

		cusparseDbsrilu02_bufferSize(cusparseHandle, order, Nb, nnzb,
			descr_M, d_bVals, d_bRows, d_bCols, BLOCK_SIZE, info_M, &d_bufferSize_M);
		cusparseDbsrsv2_bufferSize(cusparseHandle, order, operation, Nb, nnzb,
			descr_L, d_bVals, d_bRows, d_bCols, BLOCK_SIZE, info_L, &d_bufferSize_L);
		cusparseDbsrsv2_bufferSize(cusparseHandle, order, operation, Nb, nnzb,
			descr_U, d_bVals, d_bRows, d_bCols, BLOCK_SIZE, info_U, &d_bufferSize_U);
		cudaCheckLastError();
		d_bufferSize = std::max(d_bufferSize_M, std::max(d_bufferSize_L, d_bufferSize_U));
		
		cudaMalloc((void**)&d_buffer, d_bufferSize);

		// analysis of ilu LU decomposition
		cusparseDbsrilu02_analysis(cusparseHandle, order, \
			Nb, nnzb, descr_B, d_bVals, d_bRows, d_bCols, \
			BLOCK_SIZE, info_M, policy, d_buffer);

		int structural_zero;
		cusparseStatus_t status = cusparseXbsrilu02_zeroPivot(cusparseHandle, info_M, &structural_zero);
		if(CUSPARSE_STATUS_ZERO_PIVOT == status){
			return false;
		}

		// analysis of ilu apply
		cusparseDbsrsv2_analysis(cusparseHandle, order, operation, \
			Nb, nnzb, descr_L, d_bVals, d_bRows, d_bCols, \
			BLOCK_SIZE, info_L, policy, d_buffer);

		cusparseDbsrsv2_analysis(cusparseHandle, order, operation, \
			Nb, nnzb, descr_U, d_bVals, d_bRows, d_bCols, \
			BLOCK_SIZE, info_U, policy, d_buffer);
		cudaCheckLastError("Could not analyse level information");

		if(verbosity > 2){
			cudaStreamSynchronize(stream);
			t2 = second();
			std::ostringstream out;
			out << "cusparseSolver::analyse_matrix(): " << t2-t1 << " s";
			OpmLog::info(out.str());
		}

		return true;
	} // end analyse_matrix()

	bool cusparseSolverBackend::create_preconditioner(){

		double t1, t2;
		if(verbosity > 2){
			t1 = second();
		}

		d_mCols = d_bCols;
		d_mRows = d_bRows;
		cusparseDbsrilu02(cusparseHandle, order, \
			Nb, nnzb, descr_M, d_mVals, d_mRows, d_mCols, \
			BLOCK_SIZE, info_M, policy, d_buffer);

		int structural_zero;
		// cusparseXbsrilu02_zeroPivot() calls cudaDeviceSynchronize()
		cusparseStatus_t status = cusparseXbsrilu02_zeroPivot(cusparseHandle, info_M, &structural_zero);
		if(CUSPARSE_STATUS_ZERO_PIVOT == status){
			return false;
		}

		if(verbosity > 2){
			cudaStreamSynchronize(stream);
			t2 = second();
			std::ostringstream out;
			out << "cusparseSolver::create_preconditioner(): " << t2-t1 << " s";
			OpmLog::info(out.str());
		}
		return true;
	} // end create_preconditioner()


	void cusparseSolverBackend::solve_system(BdaResult &res){
		// actually solve
		gpu_pbicgstab(res);
		cudaStreamSynchronize(stream);
		cudaCheckLastError("Something went wrong during the GPU solve");
	} // end solve_system()


	// copy result to host memory
	// caller must be sure that x is a valid array
	void cusparseSolverBackend::post_process(double *x){

		if(!initialized){
			cudaHostRegister(x, N * sizeof(double), cudaHostRegisterDefault);
		}

		double t1, t2;
		if(verbosity > 2){
			t1 = second();
		}

		cudaMemcpyAsync(x, d_x, N * sizeof(double), cudaMemcpyDeviceToHost, stream);
		cudaStreamSynchronize(stream);

		if(verbosity > 2){
			t2 = second();
			std::ostringstream out;
			out << "cusparseSolver::post_process(): " << t2-t1 << " s";
			OpmLog::info(out.str());
		}
	} // end post_process()


	typedef cusparseSolverBackend::cusparseSolverStatus cusparseSolverStatus;

    cusparseSolverStatus cusparseSolverBackend::solve_system(int N, int nnz, int dim, double *vals, int *rows, int *cols, double *b, BdaResult &res){    	
		if(initialized == false){
			initialize(N, nnz, dim);
			copy_system_to_gpu(vals, rows, cols, b);
		}else{
			update_system_on_gpu(vals, b);
		}
		if(analysis_done == false){
			if(!analyse_matrix()){
				return cusparseSolverStatus::CUSPARSE_SOLVER_ANALYSIS_FAILED;
			}
		}
		reset_prec_on_gpu();
		if(create_preconditioner()){
			solve_system(res);
		}else{
			return cusparseSolverStatus::CUSPARSE_SOLVER_CREATE_PRECONDITIONER_FAILED;
		}
		return cusparseSolverStatus::CUSPARSE_SOLVER_SUCCESS;
    }


}


