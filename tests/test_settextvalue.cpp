/*
 * AbstractByteBuffer::setTextValue: what it may and may not touch.
 *
 * It used to write one byte more than the text and its terminator
 * (textPtr[newLen] = '\0'), which
 *  - zeroed the byte after a same-length name (the first byte of whatever
 *    follows it in the image: the next name, or VirtualSize behind a section's
 *    8-byte name),
 *  - wrote one byte past the end of the buffer when the text sat at its end.
 * Buffers here are exactly as large as their content (padding 0), so under
 * AddressSanitizer a stray write is a heap-buffer-overflow, and without it the
 * sentinels behind each field catch the same thing.
 *
 * Plain asserts rather than a framework: the library has none, and a test that
 * needs one more dependency is a test that is not built.
 */
#include <cstdio>
#include <cstring>
#include <string>
#include <bearparser/bearparser.h>

namespace {

	int failures = 0;

#define CHECK(cond) do { if (!(cond)) { \
		std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

	const unsigned char SENTINEL = 0xAA;

	/** An exact-size buffer holding @p bytes (no padding behind it). */
	ByteBuffer* make(const char *bytes, size_t n)
	{
		return new ByteBuffer(reinterpret_cast<BYTE*>(const_cast<char*>(bytes)),
			static_cast<bufsize_t>(n), /*padding*/ 0);
	}

	char* at(ByteBuffer &b, size_t off)
	{
		return reinterpret_cast<char*>(b.getContent()) + off;
	}

	unsigned char byteAt(ByteBuffer &b, size_t off)
	{
		return b.getContent()[off];
	}

	void sameLengthRenameKeepsTheNextByte()
	{
		// "abc\0" followed by the first byte of whatever comes next
		const char init[] = { 'a', 'b', 'c', 0, 'X' };
		ByteBuffer *b = make(init, sizeof(init));
		CHECK(b->setTextValue(at(*b, 0), "abd"));
		CHECK(std::strcmp(at(*b, 0), "abd") == 0);
		CHECK(byteAt(*b, 4) == 'X');
		delete b;
	}

	void shorterRenameWithAFieldLimitClearsOnlyTheField()
	{
		const char init[] = { 'a', 'b', 'c', 'd', 'e', 'f', 0, 'Z' };
		ByteBuffer *b = make(init, sizeof(init));
		CHECK(b->setTextValue(at(*b, 0), "ab", 7));
		const unsigned char expected[] = { 'a', 'b', 0, 0, 0, 0, 0, 'Z' };
		CHECK(std::memcmp(b->getContent(), expected, sizeof(expected)) == 0);
		delete b;
	}

	// A section name: 8 bytes, not necessarily terminated, then VirtualSize.
	void aSevenCharacterNameInAnEightByteFieldLeavesTheNextField()
	{
		unsigned char init[12];
		std::memset(init, 'x', 8);
		std::memset(init + 8, SENTINEL, 4);
		ByteBuffer *b = make(reinterpret_cast<char*>(init), sizeof(init));
		CHECK(b->setTextValue(at(*b, 0), "1234567", 8));
		CHECK(std::memcmp(at(*b, 0), "1234567\0", 8) == 0);
		for (int i = 8; i < 12; i++) CHECK(byteAt(*b, i) == SENTINEL);
		delete b;
	}

	void aNameThatFillsTheFieldExactlyHasNoTerminatorAndLeavesTheNextField()
	{
		unsigned char init[12];
		std::memset(init, 'x', 8);
		std::memset(init + 8, SENTINEL, 4);
		ByteBuffer *b = make(reinterpret_cast<char*>(init), sizeof(init));
		CHECK(b->setTextValue(at(*b, 0), "12345678", 8));
		CHECK(std::memcmp(at(*b, 0), "12345678", 8) == 0);
		for (int i = 8; i < 12; i++) CHECK(byteAt(*b, i) == SENTINEL);
		delete b;
	}

	void aNameLongerThanTheFieldIsCutAtTheFieldAndNoFurther()
	{
		unsigned char init[12];
		std::memset(init, 'x', 8);
		std::memset(init + 8, SENTINEL, 4);
		ByteBuffer *b = make(reinterpret_cast<char*>(init), sizeof(init));
		CHECK(b->setTextValue(at(*b, 0), "1234567890", 8));
		CHECK(std::memcmp(at(*b, 0), "12345678", 8) == 0);
		for (int i = 8; i < 12; i++) CHECK(byteAt(*b, i) == SENTINEL);
		delete b;
	}

	// The text sits in the last bytes of the buffer: nothing may be written behind it.
	void aStringAtTheEndOfTheBufferIsRenamedWithoutOverflowing()
	{
		const char init[] = { 'a', 'b', 'c', 0 };
		ByteBuffer *b = make(init, sizeof(init));
		CHECK(b->setTextValue(at(*b, 0), "abd"));          // same length, no room behind
		CHECK(std::strcmp(at(*b, 0), "abd") == 0);
		delete b;
	}

	void aTextThatDoesNotFitIsRefusedAndNothingChanges()
	{
		const char init[] = { 'a', 'b', 'c', 0 };
		ByteBuffer *b = make(init, sizeof(init));
		CHECK(!b->setTextValue(at(*b, 0), "abcd"));         // needs 5 bytes, 4 exist
		CHECK(std::memcmp(b->getContent(), init, sizeof(init)) == 0);
		delete b;
	}

	void theSameTextIsNotWritten()
	{
		const char init[] = { 'a', 'b', 'c', 0, 'X' };
		ByteBuffer *b = make(init, sizeof(init));
		CHECK(!b->setTextValue(at(*b, 0), "abc"));
		CHECK(std::memcmp(b->getContent(), init, sizeof(init)) == 0);
		delete b;
	}

	void aPointerOutsideTheBufferIsRefused()
	{
		const char init[] = { 'a', 'b', 'c', 0 };
		ByteBuffer *b = make(init, sizeof(init));
		char elsewhere[8] = "abc";
		CHECK(!b->setTextValue(elsewhere, "xyz"));
		CHECK(!b->setTextValue(NULL, "xyz"));
		delete b;
	}

}; // namespace

int main()
{
	sameLengthRenameKeepsTheNextByte();
	shorterRenameWithAFieldLimitClearsOnlyTheField();
	aSevenCharacterNameInAnEightByteFieldLeavesTheNextField();
	aNameThatFillsTheFieldExactlyHasNoTerminatorAndLeavesTheNextField();
	aNameLongerThanTheFieldIsCutAtTheFieldAndNoFurther();
	aStringAtTheEndOfTheBufferIsRenamedWithoutOverflowing();
	aTextThatDoesNotFitIsRefusedAndNothingChanges();
	theSameTextIsNotWritten();
	aPointerOutsideTheBufferIsRefused();

	if (failures) {
		std::printf("%d check(s) failed\n", failures);
		return 1;
	}
	std::printf("All passed\n");
	return 0;
}
