#pragma once

#include <mpi.h>
#include <vector>

int mpi_sync_global(MPI_Comm comm) {
	return MPI_Barrier(comm);
}

int mpi_sync_global() {
	return mpi_sync_global(MPI_COMM_WORLD);
}

template<typename T>
int mpi_get_global_rank(T* value) {
	return MPI_Comm_rank(MPI_COMM_WORLD, value);
}

int mpi_get_global_rank() {
	int val;
	mpi_get_global_rank(&val);
	return val;
}

template<typename T, int tag = 0>
int mpi_send_global(T* data, int count, int dest) {
	return MPI_Send(data, sizeof(T) * count, MPI_BYTE, dest, tag, MPI_COMM_WORLD);
}

template<typename T, int tag = 0>
int mpi_send_global(std::vector<T>& vec, int dest) {
	return mpi_send_global(vec.data(), vec.size(), dest);
}

template<typename T, int tag = 0>
int mpi_receive_global(T* data, int count, int src) {
	return MPI_Recv(data, sizeof(T) * count, MPI_BYTE, src, tag, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
}

template<typename T, int tag = 0>
int mpi_receive_global(T* data, size_t count, int src) {
	return mpi_receive_global<T, tag>(data, static_cast<int>(count), src);
}

template<int tag = 0>
void mpi_send_size(int dst, size_t size) {
	mpi_send_global(&size, 1, dst);
}

template<int tag = 0>
size_t mpi_receive_size(int src) {
	size_t size;
	mpi_receive_global(&size, 1, src);
	return size;
}

