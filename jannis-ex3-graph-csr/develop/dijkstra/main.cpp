#include <algorithm>
#include <cassert>
#include <iostream>
#include <fstream>
#include <random>
#include <sstream>
#include <chrono>
#include <cstring>
#include <string>
#include <tuple>
#include <vector>
#include <omp.h>
#include <limits>
#include <experimental/filesystem>
#include "d_ary_addressable_int_heap.hpp"

//___________________________________________________________________________
// read the unweighted graph
template<typename F>
void read_graph_unweighted(std::istream &ins, F fn) {
  std::string line;
  bool seen_header = false;
  while (std::getline(ins, line)) {
    if(line.empty())
      continue;
    if(line.front() == '%')
      continue;
    
    std::istringstream ls(line);
    unsigned int u, v;
    if (!(ls >> u >> v))
      throw std::runtime_error("Parse error while reading input graph");
    
    if(!seen_header) {
      seen_header = true;
      continue;
    }
    
    fn(u, v);
  }
}

//___________________________________________________________________________
// csr_matrix struct
struct csr_matrix {
  unsigned int n;
  unsigned int m;
  std::vector<unsigned int> ind;
  std::vector<unsigned int> cols;
  std::vector<float> weights;
};

//___________________________________________________________________________
// operator for matrix comparison -> easy check for transpose?
bool operator ==(const csr_matrix& lhs, const csr_matrix& rhs) {
  return std::tie(lhs.m, lhs.n, lhs.ind, lhs.cols, lhs.weights)
    == std::tie(rhs.m, rhs.n, rhs.ind, rhs.cols, rhs.weights);
}

bool operator !=(const csr_matrix& lhs, const csr_matrix& rhs) { return !(lhs == rhs); }

//___________________________________________________________________________
// transform coordinates to a csr_matrix format
csr_matrix coordinates_to_csr(unsigned int n,
			      std::vector<std::tuple<unsigned int, unsigned int, float>> cv) {
  unsigned int m = cv.size();
  
  csr_matrix mat;
  mat.n = n;
  mat.m = m;
  mat.ind.resize(n + 1);
  mat.cols.resize(m);
  mat.weights.resize(m);
  
  // Count the number of neighbors of each node.
  for(auto ct : cv) {
    auto u = std::get<0>(ct);
    ++mat.ind[u];
  }
  
  // Form the prefix sum.
  for(unsigned int i = 1; i <= n; ++i)
    mat.ind[i] += mat.ind[i - 1];
  assert(mat.ind[n] == m);
  
  // Insert the entries of the matrix in reverse order.
  for(auto it = cv.rbegin(); it != cv.rend(); ++it) {
    auto u = std::get<0>(*it);
    auto v = std::get<1>(*it);
    auto weight = std::get<2>(*it);
    mat.cols[mat.ind[u] - 1] = v;
    mat.weights[mat.ind[u] - 1] = weight;
    --mat.ind[u];
  }
  
  return mat;
}

//___________________________________________________________________________
// transpose a given csr matrix
csr_matrix transpose(const csr_matrix& inp) {
  csr_matrix out = {
    inp.n,
    inp.m,
    std::vector <unsigned int>(inp.ind.size() + 1, 0.),
    std::vector <unsigned int>(inp.m, 0),
    std::vector <float>       (inp.weights.size(), 0.)
  };

  // count the entries per column for the new indice vector
  for (auto &col : inp.cols) {
    ++out.ind[col+2];
  }

  // running sum the new indice vector
  for (std::size_t i = 2; i < out.ind.size(); ++i) {
    out.ind[i] += out.ind[i-1];
  }

  // transform the matrix row by row
  for (std::size_t row = 0; row < inp.n; ++row) {
    #pragma omp parallel for
    for (std::size_t idx = inp.ind[row]; idx < inp.ind[row+1]; ++idx) {
      std::size_t idx_new = out.ind[inp.cols[idx] + 1]++;
      out.weights[idx_new] = inp.weights[idx];
      out.cols   [idx_new] = row;
    }
  }

  // remove last because of out.ind[]++ in loop above
  out.ind.pop_back();
  return out;
}

//__________________________________________________________________________
// running the dijkstra algorihtm
template <typename key_type, unsigned arity = 2>
std::vector <float> dijkstra(csr_matrix& mat, csr_matrix& mat_tr, key_type start) {
  
  // init priority vector, comparison function for priorities and the d-ary heap
  std::vector <float> v_pri(mat.n, std::numeric_limits<float>::max());

  auto cmp_pri = [&] (key_type lhs, key_type rhs) {
    return v_pri[lhs] < v_pri[rhs];
  };
  
  DAryAddressableIntHeap <key_type, arity, std::function<bool(key_type, key_type)>> q(cmp_pri);

  // start the queue
  v_pri[start] = 0.0; // set the starting point to zero
  q.push(start);
  while(!q.empty()) {
    key_type u = q.extract_top();

    // update the neighbours for the non zero row elements for a matrix
    auto update_neighbours = [&] (csr_matrix &mat) {
      for (key_type idx = mat.ind[u]; idx < mat.ind[u+1]; ++idx) {
	key_type v = mat.cols[idx];
	if (v_pri[u] + mat.weights[idx] < v_pri[v]) {
	  v_pri[v] = v_pri[u] + mat.weights[idx];
	  q.update(v);
	}
      }
    };

    // 1. -> non zero elements in csr_matrix row
    update_neighbours(mat);

    // 2. -> non zero elements in csr_matrix col
    // -> non zero elements in csr_matrix_tr row
    update_neighbours(mat_tr);
  }
  
  // return the priority values
  return v_pri;
}

//___________________________________________________________________________
// get a spcific element
float get(const csr_matrix& m, std::size_t row, std::size_t col) {
  // check for validity
  if (m.n < row or m.n < col) {
    throw std::runtime_error(std::string(__FUNCTION__) + ": m.n <= row ro m.n <= col");
  }

  // get the row indice
  if (m.ind[row] != m.ind[row+1]) {
    for (std::size_t i = m.ind[row]; i < m.ind[row+1]; ++i) {
      if (col == m.cols[i]) {
	return m.weights[i];
      }
    }
    return 0.0;
  }

  // row does not contain elements
  else {
    return 0.0;
  }
}

//__________________________________________________________________________________________
// check the transpose for the matrix
// this in combination with get() might not be the best idea
bool check_transpose(const csr_matrix& mat, const csr_matrix& mat_tr) {
  for (std::size_t row = 0; row < (mat.ind.size()-1); ++row)
    for (std::size_t idx = mat.ind[row]; idx < mat.ind[row+1]; ++idx)
      if (get(mat, row, mat.cols[idx]) != get(mat_tr, mat.cols[idx], row))
	return false;
  return true;
}

//___________________________________________________________________________
// evalualtion of matrix transpose
void evaluate_transpose(csr_matrix& mat, std::string_view graph) {
  
  // time the actual transpose
  auto t_start = std::chrono::high_resolution_clock::now();
  csr_matrix mat_tr = transpose(mat);
  auto t_trans = std::chrono::high_resolution_clock::now() - t_start;

  // check if the transpose of the transpose is the original matrix
  std::size_t errors = 0;
  if (!check_transpose(mat, mat_tr)) {
    ++errors;
  }
  std::cerr << "There were " << errors << " errors" << std::endl;
  assert(!errors);
  
  // output of data
  std::cout << "threads: " << omp_get_max_threads() << std::endl;
  std::cout << "algo: transpose" << std::endl;
  std::cout << "graph: " << graph << std::endl;
  std::cout << "time: "
	    << std::chrono::duration_cast<std::chrono::milliseconds>(t_trans).count()
	    << " # ms" << std::endl;
}

//__________________________________________________________________________
// evaluate dijkstra
/*
*/
void evaluate_dijkstra(csr_matrix& mat, std::string_view graph) {

  // number of repitions
  std::size_t n_repeat = 1000;
  
  // transpose matrix for easier neighbours
  csr_matrix mat_tr = transpose(mat);
  
  // "random numbers"
  std::mt19937 prng{42};
  std::uniform_int_distribution<std::size_t> distrib {0, mat.n};
  std::size_t ref = distrib(prng);
  std::vector <float> v_pri_ref = dijkstra<unsigned int, 2>(mat, mat_tr, ref), v_pri_com;
  
  // time the dijkstra algorithm 1000 times for random starting points
  std::chrono::duration <double> t_comp;
  std::size_t errors = 0;
  for (std::size_t i = 0; i < n_repeat; ++i) {
    // time instance
    auto t_start = std::chrono::high_resolution_clock::now();
    v_pri_com = dijkstra<unsigned int, 2>(mat, mat_tr, ref);
    auto t_dijk = std::chrono::high_resolution_clock::now() - t_start;
    t_comp += t_dijk;
    
    // check for correctness in float precision
    if ((float)(v_pri_com[ref] - v_pri_ref[ref]) > std::numeric_limits<float>::epsilon()) {
      ++errors;
    }
  }
  std::cerr << "There were " << errors << " errors" << std::endl;
  assert(!errors);
  
  // output of data
  std::cout << "threads: " << omp_get_max_threads() << std::endl;
  std::cout << "algo: dijkstra" << std::endl;
  std::cout << "graph: " << graph << std::endl;
  std::cout << "time: "
	    << std::chrono::duration_cast<std::chrono::milliseconds>(t_comp).count() / n_repeat
	    << " # ms" << std::endl;
}

//___________________________________________________________________________
// main function to run the experiments
static const char *usage_text =
			  "Usage: dijkstra [OPTIONS]\n"
			  "Possible OPTIONS are:\n"
			  "    --algo ALGORITHM\n"
			  "        Select an algorithm {transpose,dijkstra}.";


int main(int argc, char **argv) {

  //___________________________________________________________________
  // parse the argument options
  std::string_view algorithm, graph;
    
  auto error = [] (const char *text) {
    std::cerr << usage_text << "Usage error: " << text << std::endl;
    exit(2);
  };
  
  // Argument for unary options.
  const char *arg;
  
  // Parse all options here.
  
  char **p = argv + 1;
  
  auto handle_unary_option = [&] (const char *name) -> bool {
    assert(*p);
    if(std::strcmp(*p, name))
      return false;
    ++p;
    if(!(*p))
      error("expected argument for unary option");
    arg = *p;
    ++p;
    return true;
  };
  
  while(*p && !std::strncmp(*p, "--", 2)) {
    if(handle_unary_option("--algo")) {
      algorithm = arg;
    } else if (handle_unary_option("--graph")) {
      graph = arg;
    } else {
      error("unknown command line option");
    }
  }
  
  if(*p)
    error("unexpected arguments");

  // read in the network
  std::string file;
  if (graph.empty()) {
    error("no graph specified");
  }
  if (graph == "cit_patent") {
    file = "/vol/fob-vol7/nebenf18/schuermj/algo/exp_algo/ex3-graph-csr/cit-patent.edges";
  } else if (graph == "road_texas") {
    file = "/vol/fob-vol7/nebenf18/schuermj/algo/exp_algo/ex3-graph-csr/roadNet-TX.mtx";
  } else {
    error("unknown graph");
  }

  //___________________________________________________________________
  // read in the graph data
  // open the file
  std::ifstream ins(file);
  if (!ins) {
    std::cerr << "Can't open file {" + file + "}" << std::endl;
    assert(0);
  }
  
  std::vector<std::tuple<unsigned int, unsigned int, float>> cv;
  std::mt19937 prng{42};
  std::uniform_real_distribution<float> distrib{0.0f, 1.0f};
  read_graph_unweighted(ins, [&] (unsigned int u, unsigned int v) {
      // Generate a random edge weight in [a, b).
      cv.push_back({u, v, distrib(prng)});
    });
  
  // Determine n as the maximal node ID.
  unsigned int n = 0;
  for(auto ct : cv) {
    auto u = std::get<0>(ct);
    auto v = std::get<1>(ct);
    if(u > n)
      n = u;
    if(v > n)
      n = v;
  }
  n += 1; // add one because counting starts at zero
  auto mat = coordinates_to_csr(n, std::move(cv));
    
  // Verify that options are correct and run the algorithm.
  if(algorithm.empty())
    error("no algorithm specified");
  if(algorithm == "transpose") {
    evaluate_transpose(mat, graph);
  } else if(algorithm == "dijkstra") {
    evaluate_dijkstra(mat, graph);
  } else {
    error("unknown algorithm");
  }  
}
