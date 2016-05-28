#include <cstdint>
#include <tuple>
#include <utility>
#include <limits>
#include <vector>

namespace pack {
	enum class endian { little, big, native = little };

	namespace exception {
		class base: public std::exception {
			std::string message;
			protected:
			explicit base(const std::string& _message) : message(_message) { }
			public:
			virtual const char* what() const noexcept override final {
				return message.c_str();
			}
		};
		class invalid_input : public base {
			public:
			explicit invalid_input(const std::string& _message) : base(_message) { }
		};
		class out_of_bounds : public base {
			public:
			explicit out_of_bounds(const std::string& type) : base("Insufficient data in buffer to unpack " + type) {
			}
		};
	}

	namespace {
		template<typename head_type, typename... tail_types> struct follow_up {
			template<typename head_argument, typename... tail_arguments> static std::string pack(const head_argument& data, const tail_arguments&... arguments) {
				return head_type::pack(std::move(data)) + follow_up<tail_types...>::pack(arguments...);
			}
			static std::tuple<typename head_type::data_type, typename tail_types::data_type...> unpack(std::string::const_iterator current, const std::string::const_iterator& end) {
				auto first = std::make_tuple(head_type::unpack(current, end).decode());
				return tuple_cat(std::move(first), follow_up<tail_types...>::unpack(current, end));
			}
		};
		template<typename head_type> struct follow_up<head_type> {
			template<typename head_argument> static std::string pack(const head_argument& data) {
				return head_type::pack(data);
			}
			static std::tuple<typename head_type::data_type> unpack(std::string::const_iterator current, const std::string::const_iterator& end) {
				return std::make_tuple(head_type::unpack(current, end).decode());
			}
		};

		template<enum endian order> void byte_copy(char* target, const char* begin, const char* end);
		template<> void byte_copy<endian::big>(char* target, const char* begin, const char* end) {
			auto current = end;
			while (--current >= begin) {
				*target++ = *current;
			}
		}

		template<> void byte_copy<endian::native>(char* target, const char* begin, const char* end) {
			auto current = begin;
			while(current < end)
				*target++ = *current++;
		}

		template<enum endian order> void byte_copy(char* target, const std::string::const_iterator& begin, const std::string::const_iterator& end) {
			return byte_copy<order>(target, &*begin, &*end);
		}
	}

	struct piece {
		const std::string::const_iterator begin;
		const std::string::const_iterator end;
		piece(const std::string::const_iterator& _begin, const std::string::const_iterator& _end) : begin(_begin), end(_end) { }
	};

	template<typename raw_type, endian order = endian::native> struct integral {
		using data_type = raw_type;
		struct decoder : public piece {
			using piece::piece;
			raw_type decode() const noexcept {
				raw_type temp(0);
				byte_copy<order>(reinterpret_cast<char*>(&temp), begin, end);
				return temp;
			}
		};
		static std::string pack(raw_type value) noexcept {
			char buffer[sizeof(raw_type)];
			byte_copy<order>(buffer, reinterpret_cast<const char*>(&value), reinterpret_cast<const char*>(&value + 1));
			return std::string(buffer, buffer + sizeof buffer);
		}
		static decoder unpack(std::string::const_iterator& current, const std::string::const_iterator& end) {
			static const std::string null(sizeof(raw_type), '\0');

			if (current + sizeof(raw_type) <= end) {
				auto begin = current;
				current += sizeof(raw_type);
				return decoder(begin, current);
			}
			else
				throw exception::out_of_bounds("integer");
		}
	};

	template<typename raw_type = unsigned long long, endian order = endian::little> struct compressed;
	template<typename raw_type> struct compressed<raw_type, endian::little> {
		using data_type = raw_type;
		static const size_t block_size = 1 << 7;
		static const size_t mask = block_size - 1;

		struct decoder : public piece {
			using piece::piece;
			raw_type decode() const noexcept {
				raw_type factor = 1, ret = 0;
				auto current = begin;
				while (current < end) {
					ret += (static_cast<unsigned char>(*current++) & mask) * factor;
					factor *= block_size;
				}
				return ret;
			}
		};

		static std::string pack(raw_type value) noexcept {
			std::string ret;
			if (value == 0)
				return std::string("\0", 1);
			while (value) {
				unsigned char current = value % block_size;
				ret.push_back(current | block_size);
				value /= block_size;
			}
			if (ret.size())
				*(ret.end() - 1) &= mask;
			return ret;
		}
		static decoder unpack(std::string::const_iterator& current, const std::string::const_iterator& end) {
			std::string::const_iterator begin = current;
			while (static_cast<unsigned char>(*current++) & block_size) {
				if (current == end)
					throw exception::out_of_bounds("compressed integer");
			}
			return decoder(begin, current);
		}
	};

	namespace padding {
		struct none {
			static std::string add_padding(const std::string& value, size_t length) {
				if (value.size() != length)
					throw exception::invalid_input("Packed string should be of length " + std::to_string(length));
				return value;
			}
			static std::string strip_padding(std::string value) noexcept {
				return value;
			}
		};

		template<char character> struct byte_padder {
			static std::string add_padding(std::string value, size_t length) {
				if (value.length() == length)
					return value;
				else if (value.size() > length)
					throw exception::invalid_input("Can't pack string longer than fixed length");
				else
					return value.append(length - value.size(), character);
			}
			static std::string strip_padding(std::string value) noexcept {
				size_t end = value.find_last_not_of(character);
				if (end == std::string::npos)
					return std::string();
				else if (value.begin() + end == value.end())
					return value;
				else
					return std::string(value.begin(), value.begin() + end + 1);
			}
		};
		using null = byte_padder<'\0'>;
		using space = byte_padder<' '>;
	}

	template<int length, typename pad = padding::none> struct fixed_string {
		using data_type = std::string;
		struct decoder : public piece {
			using piece::piece;
			std::string decode() const noexcept {
				return pad::strip_padding(std::string(begin, end));
			}
		};
		static std::string pack(std::string value) {
			return pad::add_padding(value, length);
		}
		static decoder unpack(std::string::const_iterator& current, const std::string::const_iterator& end) {
			if (current + length <= end) {
				auto begin = current;
				current += length;
				return decoder(begin, current);
			}
			else
				throw exception::out_of_bounds("fixed_string");
		}
	};

	template<typename length_encoder = integral<unsigned, endian::little>> struct varchar {
		using data_type = std::string;
		struct decoder : public piece {
			using piece::piece;
			std::string decode() const noexcept {
				return std::string(begin, end);
			}
		};
		static std::string pack(std::string value) {
			return length_encoder::pack(value.size()) + value;
		}
		static decoder unpack(std::string::const_iterator& current, const std::string::const_iterator& end) {
			size_t length = length_encoder::unpack(current, end).decode();
			if (unsigned(end - current) >= length) {
				auto begin = current;
				current += length;
				return decoder(begin, current + length);
			}
			else
				throw exception::out_of_bounds("varchar");
		}
	};

	template<typename element_encoder, typename length_encoder> struct sequence {
		class decoder {
			using element_type = decltype(std::declval<element_encoder::decoder>().decode());
			using decoder_type = typename element_encoder::decoder;
			std::vector<decoder_type> elems;
			public:
			decoder(std::vector<decoder_type> _elems) : elems(_elems) { }
			std::vector<element_type> decode() const {
				std::vector<element_type> ret;
				for (const auto& piece : elems)
					ret.push_back(ret.decode());
				return ret;
			}
		};
		template<typename argument_type> static std::string pack(const std::vector<argument_type>& elements) {
			std::string ret = length_encoder::pack(elements.size());
			for (const auto& elem: elements)
				ret += element_encoder::pack(elem);
			return ret;
		}
		static decoder unpack(std::string::const_iterator& current, const std::string::const_iterator& end) {
			size_t length = length_encoder::unpack(current, end).decode();
			std::vector<piece> pieces;
			while (length--)
				pieces.push_back(element_encoder::unpack(current, end));
			return decoder(std::move(pieces));
		}
	};

	template<typename... elements> class format {
		typedef follow_up<elements...> packer;

		public:
		template<typename... argument_types> static std::string pack(const argument_types&... arguments) {
			return packer::pack(arguments...);
		}
		static std::tuple<typename elements::data_type...> unpack(const std::string& packed) {
			std::string::const_iterator begin = packed.begin();
			return packer::unpack(begin, packed.end());
		}
	};
}
