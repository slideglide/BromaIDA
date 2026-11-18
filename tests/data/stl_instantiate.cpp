#include <array>
#include <atomic>
#include <list>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>
struct Foo {
    int a;
};
template class std::vector<int>;
template class std::vector<std::string>;
template class std::vector<Foo*>;
template class std::list<int>;
template class std::list<std::string>;
template class std::map<int, std::string>;
template class std::map<std::string, Foo>;
template class std::set<int>;
template class std::set<std::string>;
template class std::unordered_map<int, float>;
template class std::unordered_map<std::string, std::string>;
template class std::unordered_map<int, std::vector<int>>;
template class std::unordered_set<int>;
template class std::unordered_set<std::string>;
template class std::basic_string<char, std::char_traits<char>, std::allocator<char>>;
template class std::basic_string<wchar_t, std::char_traits<wchar_t>, std::allocator<wchar_t>>;

// IDAClang instantiates the internal node types of the containers too, which a plain parse of the containers does
// not: this is where the MSVC list node once asked allocator_traits<void*> to rebind
#if __has_include(<xmemory>)
template struct std::_List_node<int, void*>;
template struct std::_List_node<std::string, void*>;
template struct std::_List_node<std::pair<int const, std::vector<int>>, void*>;
#endif
