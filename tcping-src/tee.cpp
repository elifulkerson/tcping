
#include <fstream>
#include <stdio.h>
#include <stdarg.h>
#include <vector>

/*

Don't make fun of me, Unix guys.  I know about `which tee`.  Tee isn't always available on windows systems, however.

*/

class tee
{
public:
	tee();
	~tee(void);
	void Open(char* filename);
	void OpenAppend(char* filename);
	void Close();
	void p(const char* text);
	void pf(const char* format, ...);
	void enable(bool onoff);

private:
	std::ofstream outfile;
	int flag;
	bool enable_output;
};


tee::tee()
{
	flag = 0;
	enable_output = true;
}

tee::~tee()
{
	this->Close();
}

void tee::OpenAppend(char*filename)
{
	if (flag != 0) {
		outfile.close();
	}
	outfile.open(filename, std::ofstream::out | std::ofstream::app);
	flag = 1;
}


void tee::Open(char*filename)
{
	if (flag != 0) {
		outfile.close();
	}
	outfile.open(filename);
	flag = 1;
}

void tee::Close()
{
	if (flag != 0) {
		outfile.close();
	}
	flag = 0;
}

void tee::p(const char* text)
{
	if (enable_output == false) {
		return;
	}

	printf("%s", text);
	if (flag == 1) {
		outfile << text;
		outfile.flush();
	}
	fflush(stdout);
}

void tee::pf(const char* format, ...)
{
	if (enable_output == false) {
		return;
	}

	// Size the buffer to the formatted output so long lines are not truncated.
	// Handles both C99 vsnprintf (returns the required length) and older MSVC
	// runtimes (return < 0 on truncation) by growing the buffer until it fits.
	std::vector<char> buffer(256);
	while (true) {
		va_list args;
		va_start(args, format);
		int needed = vsnprintf(buffer.data(), buffer.size(), format, args);
		va_end(args);

		if (needed >= 0 && static_cast<size_t>(needed) < buffer.size()) {
			break;
		}
		if (buffer.size() >= (1u << 20)) {
			break;  // sanity cap; stop growing
		}
		buffer.resize(needed > 0 ? static_cast<size_t>(needed) + 1 : buffer.size() * 2);
	}

	this->p(buffer.data());
}

void tee::enable(bool onoff){
	enable_output = onoff;
}







