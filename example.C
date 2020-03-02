#include <iostream>
#include <iomanip>
#include <unistd.h>
#include <packing.h>

using namespace std;
using namespace std::literals;
using namespace packing;

static void print_string(const std::string& packed) {
	cout << "Bytes are";
	cout << hex;
	for (const char& c : packed)
		cout << " " << hex << setw(2) << setfill('0') << int(uint8_t(c));
	cout << dec << endl;
}

int main(int, char**) {
	using packer = format<integral<16, sign::no, endian::big>, fixed_string<2, padding::space>, compressed<sign::no, endian::little>, varchar<compressed<>>>;
	try {
		string packed = packer::pack(1u, "a", 300u, "abc");
		print_string(packed);

		std::string::const_iterator end;
		auto [ integer, letter, compression, stringy ] = packer::unpack(packed, end);
		cout << "Original value was " << integer << endl;
		cout << "String value was '" << letter << "'" << endl;
		cout << "Compressed integer was " << compression << endl;
		cout << "Second string value was '" << stringy << "'" << endl;
		string at_end = (end == packed.end() ? ""s : "not "s);
		cout << "Iterator was " << at_end << "at the end" << endl;

		auto p2 = compressed<sign::no, endian::little, 32>::pack(uint16_t(65535));
		auto begin = p2.cbegin();
		uint16_t value = compressed<sign::no, endian::little, 16>::unpack(begin, p2.cend());

		cout << "Value was " << value << endl;

		using packer3 = format<integral<16, sign::no>>;
		auto p3 = packer3::pack(static_cast<unsigned short>(65535));
		auto [ value2 ] = packer3::unpack(p3);

		cout << "Value is still " << value2 << endl;

		auto p4 = pack<integral<16, sign::no>>(65535);
		auto [ value3 ] = unpack<integral<16, sign::no>>(p4);

		cout << "Value is still " << value3 << endl;

		return EXIT_SUCCESS;
	}
	catch (const packing::exception::base& error) {
		std::cerr << error.what() << std::endl;
		return EXIT_FAILURE;
	}
}
