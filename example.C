#include <iostream>
#include <iomanip>
#include <unistd.h>
#include <pack.h>

using namespace std;
using namespace pack;

void print_string(const std::string& packed) {
	cout << "Bytes are";
	cout << hex;
	for (const char& c : packed)
		cout << " " << hex << setw(2) << setfill('0') << int(uint8_t(c));
	cout << dec << endl;
}

int main(int, char**) {
	using packer = format<integral<16, sign::no, endian::big>, fixed_string<2, padding::space>, compressed<>, varchar<compressed<>>, current_iterator>;
	try {
		string packed = packer::pack(1, "a", 300, "abc");
		print_string(packed);

		auto tup = packer::unpack(packed);
		cout << "Original value was " << get<0>(tup) << endl;
		cout << "String value was '" << get<1>(tup) << "'" << endl;
		cout << "Compressed integer was " << get<2>(tup) << endl;
		cout << "Second string value was '" << get<3>(tup) << "'" << endl;
		string at_end = (get<4>(tup) == packed.end() ? "" : "not ");
		cout << "Iterator was " << at_end << "at the end" << std::endl;
		return EXIT_SUCCESS;
	}
	catch (const exception::base& error) {
		std::cerr << error.what() << std::endl;
		return EXIT_FAILURE;
	}
}
