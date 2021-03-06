#include <algorithm>
#include <cassert>
#include <cstring>
#include <chrono>
#include <iostream>
#include <optional>
#include <random>
#include <unordered_map>
#include <vector>

constexpr int M = 1 << 26;

// This function turns a hash into an index in [0, M).
// This computes h % M but since M is a power of 2,
// we can compute the modulo by using a bitwise AND to cut off the leading binary digits.
// As hash, we just use the integer key directly.
int hash_to_index(int h) {
    return h & (M - 1);
}

//_________________________________________________________________________________________________
// This implements a hash table that uses chaining.
struct chaining_table {
    static constexpr const char *name = "chaining";

    struct chain {
        chain *next;
        int key;
        int value;
    };

    chaining_table()
        : heads{new chain *[M]{}} { }

    // Note: to simplify the implementation, destructors and copy/move constructors are missing.

    void put(int k, int v) {
        auto idx = hash_to_index(k);
        auto p = heads[idx];

        if(!p) {
            heads[idx] = new chain{nullptr, k, v};
            return;
        }

        while(true) {
            assert(p);

            if(p->key == k) {
                p->value = v;
                return;
            }

            if(!p->next) {
                p->next = new chain{nullptr, k, v};
                return;
            }

            p = p->next;
        }
    }

    std::optional<int> get(int k) {
        auto idx = hash_to_index(k);
        auto p = heads[idx];

        while(p) {
            if(p->key == k)
                return p->value;

            p = p->next;
        }

        return std::nullopt;
    }

    chain **heads = nullptr;
};

//_________________________________________________________________________________________________
// This hash table uses linear probing.
struct linear_table {
    static constexpr const char *name = "linear";

    struct cell {
        int key;
        int value;
        bool valid = false;
    };

    linear_table()
        : cells{new cell[M]{}} { }

    // Note: to simplify the implementation, destructors and copy/move constructors are missing.

    void put(int k, int v) {
        int i = 0;

        while(true) {
            assert(i < M);

            auto idx = hash_to_index(k + i);
            auto &c = cells[idx];

            if(!c.valid) {
                c.key = k;
                c.value = v;
                c.valid = true;
                return;
            }

            if(c.key == k) {
                c.value = v;
                return;
            }

            ++i;
        }
    }

    std::optional<int> get(int k) {
        int i = 0;

        while(true) {
            assert(i < M);

            auto idx = hash_to_index(k + i);
            auto &c = cells[idx];

            if(!c.valid)
                return std::nullopt;

            if(c.key == k)
                return c.value;

            ++i;
        }
    }

    cell *cells = nullptr;
};

//_________________________________________________________________________________________________
// quadratic probing
struct quadratic_table {
    static constexpr const char *name = "quadratic";

    struct cell {
        int  key;
        int  value;
        bool valid = false;
    };

    quadratic_table() : cells { new cell[M]{} } {}

    // yes yes... destructors... 

    // insert element
    void put(int k, int v) {
        int i = 0;

        while(true) {
            assert(i < M);

            // we use factor = 1
            auto idx = hash_to_index(k + i * (i + 1));
            auto &c = cells[idx];

            if (!c.valid) {
                c.key = k;
                c.value = v;
                c.valid = true;
                return;
            }

            if (c.key == k) {
                c.value = v;
                return;
            }

            ++i;
        }
    }

    // retrieve element
    std::optional<int> get(int k) {
        int i = 0;

        while(true) {
            assert(i < M);

            // we use factor = 1
            auto idx = hash_to_index(k + i * (i + 1));
            auto &c = cells[idx];

            if (!c.valid) {
                return std::nullopt;
            }

            if (c.key == k) {
                return c.value;
            }

            ++i;
        }
    }

    cell *cells = nullptr;
};

//_________________________________________________________________________________________________
// robin hood probing
struct robin_hood_table {
    static constexpr const char *name = "robin_hood";
    int psl_max = 0;

    struct cell {
        int  key;
        int  value;
        int  psl;
        bool valid = false;
    };

    robin_hood_table() : cells { new cell[M]{} } {}

    // insert element
    void put(int k, int v) {
        int i = 0;

        while(true) {
            assert(i < M);

            auto idx = hash_to_index(k + i);
            auto &c = cells[idx];

            // -> empty -> fill in
            if (!c.valid) {
                c.key = k;
                c.value = v;
                c.psl = i;
                c.valid = true;

                if (c.psl > psl_max) {
                    psl_max = c.psl;
                }
                return;
            }

            // -> not empty
            if (c.key == k) {
                c.value = v;
                return;
            }

            // check psl, swap if our probe sequence is bigger and instert again
            if (c.psl < i) {
                std::swap(c.key, k);
                std::swap(c.value, v);
                std::swap(c.psl, i);
                if (c.psl > psl_max) {
                    psl_max = c.psl;
                }
                return this->put(k, v);
            }
            ++i;
        }
    }

    // retrieve element
    std::optional<int> get(int k) {
        int i = 0;

        while(true) {
            assert(i < M);

            auto idx = hash_to_index(k + i);
            auto &c = cells[idx];

            if ((!c.valid) or (i > psl_max)) {
                return std::nullopt;
            }

            if (c.key == k) {
                return c.value;
            }

            ++i;
        }
    }

    cell *cells = nullptr;
};

//_________________________________________________________________________________________________
// For comparsion, an implementation that uses std::unordered_map.
// This is apples-to-oranges since std::unordered_map does not respect our fill factor.
struct stl_table {
    static constexpr const char *name = "stl";

    void put(int k, int v) {
        map[k] = v;
    }

    std::optional<int> get(int k) {
        auto it = map.find(k);
        if(it == map.end())
            return std::nullopt;
        return it->second;
    }

    std::unordered_map<int, int> map;
};

//_________________________________________________________________________________________________
// Helper function to evaluate a hash table algorithm.
// You should not need to touch this.
template<typename Algo>
void evaluate(float fill_factor) {
    Algo table;

    std::mt19937 prng{42};

    int n = M * fill_factor;

    std::cerr << "Generating random data..." << std::endl;

    // First roll a pool of keys that we will use.
    std::vector<int> pool;
    {
        std::uniform_int_distribution<int> distrib;
        for(int i = 0; i < n; ++i)
            pool.push_back(distrib(prng));
    }

    // Now generate a sequence of insertions that only use keys in our pool.
    std::vector <int> misses; 
    std::vector<std::pair<int, int>> inserts;
    std::vector<std::pair<int, int>> truth; // The final values for each key, for sanity checking.

    int nv = 1;
    {
        std::uniform_int_distribution<int> distrib{0, n};
        std::unordered_map<int, int> temp;
        for(int i = 0; i < n; ++i) {
            int k = pool[distrib(prng)];
            inserts.push_back({k, nv});
            temp[k] = nv;
            ++nv;
        }

        for(auto [k, v] : temp)
            truth.push_back({k, v});

        for (auto k : pool) {
            if (temp.find(k) == temp.end())
                misses.push_back(k);
        }
    }

    std::cerr << "Performing insertions..." << std::endl;

    auto insert_start = std::chrono::high_resolution_clock::now();
    for(auto [k, v] : inserts)
        table.put(k, v);
    auto t_insert = std::chrono::high_resolution_clock::now() - insert_start;

    std::cerr << "Performing lookups..." << std::endl;

    // We want iterate through the keys in random order to avoid any potential bias.
    std::shuffle(truth.begin(), truth.end(), prng);

    int errors = 0;
    auto lookup_start = std::chrono::high_resolution_clock::now();
    for(auto [k, v] : truth) {
        auto r = table.get(k);
        if(!r) {
            ++errors;
            continue;
        }

        if(*r != v) {
            ++errors;
            continue;
        }
    }
    auto t_lookup = std::chrono::high_resolution_clock::now() - lookup_start;

    // measure timing for misses
    auto misses_start = std::chrono::high_resolution_clock::now();
    for (auto k : misses) {
        auto r = table.get(k);
        if (r) {
            ++errors;
        }
    }
    auto t_misses = std::chrono::high_resolution_clock::now() - misses_start;

    std::cerr << "There were " << errors << " errors" << std::endl;
    assert(!errors);

    std::cout << "algo: " << Algo::name << std::endl;
    std::cout << "m: " << M << std::endl;
    std::cout << "fill_factor: " << fill_factor << std::endl;
    std::cout << "time_insert: "
        << std::chrono::duration_cast<std::chrono::milliseconds>(t_insert).count()
        << " # ms" << std::endl;
    std::cout << "time_lookup: "
        << std::chrono::duration_cast<std::chrono::milliseconds>(t_lookup).count()
        << " # ms" << std::endl;
    std::cout << "time_misses: "
        << std::chrono::duration_cast<std::chrono::milliseconds>(t_misses).count()
        << " # ms" << std::endl;
}

//_________________________________________________________________________________________________
// Helper function to perform a microbenchmark.
// You should not need to touch this.
template<typename Algo>
void microbenchmark(float fill_factor) {
    Algo table;

    std::mt19937 prng{42};
    std::uniform_int_distribution<int> distrib;

    int n = M * fill_factor;

    std::cerr << "Running microbenchmark..." << std::endl;

    auto start = std::chrono::high_resolution_clock::now();
    int nv = 1;
    for(int i = 0; i < n; ++i)
        table.put(distrib(prng), nv++);
    auto t = std::chrono::high_resolution_clock::now() - start;

    std::cout << "algo: " << Algo::name << std::endl;
    std::cout << "m: " << M << std::endl;
    std::cout << "fill_factor: " << fill_factor << std::endl;
    std::cout << "time: "
        << std::chrono::duration_cast<std::chrono::milliseconds>(t).count()
        << " # ms" << std::endl;
}

static const char *usage_text =
"Usage: hashing [OPTIONS]\n"
"Possible OPTIONS are:\n"
"    --microbenchmark\n"
"        Perform microbenchmarking.\n"
"    --algo ALGORITHM\n"
"        Select an algorithm {chaining,linear,stl}.\n"
"    --fill FACTOR\n"
"        Set the fill factor.\n";

int main(int argc, char **argv) {
    bool do_microbenchmark = false;
    std::string_view algorithm;
    float fill_factor = 0.5;

    auto error = [] (const char *text) {
        std::cerr << usage_text << "Usage error: " << text << std::endl;
        exit(2);
    };

    // Argument for unary options.
    const char *arg;

    // Parse all options here.

    char **p = argv + 1;

    auto handle_nullary_option = [&] (const char *name) -> bool {
        assert(*p);
        if(std::strcmp(*p, name))
            return false;
        ++p;
        return true;
    };

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
        if(handle_nullary_option("--microbenchmark")) {
            do_microbenchmark = true;
        }else if(handle_unary_option("--algo")) {
            algorithm = arg;
        }else if(handle_unary_option("--fill")) {
            fill_factor = std::atof(arg);
        }else{
            error("unknown command line option");
        }
    }

    if(*p)
        error("unexpected arguments");

    // Verify that options are correct and run the algorithm.

    if(algorithm.empty())
        error("no algorithm specified");

    if(do_microbenchmark) {
        if(algorithm == "chaining") {
            microbenchmark<chaining_table>(fill_factor);
        }else if(algorithm == "linear") {
            microbenchmark<linear_table>(fill_factor);
        }else if(algorithm == "stl") {
            microbenchmark<stl_table>(fill_factor);
        }else if(algorithm == "quadratic") {
            microbenchmark<quadratic_table>(fill_factor);
        }else if(algorithm == "robin_hood") {
            microbenchmark<robin_hood_table>(fill_factor);
        }else{
            error("unknown algorithm");
        }
    }else{
        if(algorithm == "chaining") {
            evaluate<chaining_table>(fill_factor);
        }else if(algorithm == "linear") {
            evaluate<linear_table>(fill_factor);
        }else if(algorithm == "stl") {
            evaluate<stl_table>(fill_factor);
        }else if(algorithm == "quadratic") {
            evaluate<quadratic_table>(fill_factor);
        }else if(algorithm == "robin_hood") {
            evaluate<robin_hood_table>(fill_factor);
        }else{
            error("unknown algorithm");
        }
    }
}
