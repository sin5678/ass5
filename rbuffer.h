#pragma once
class rbuffer
{
public:
	rbuffer();
	virtual ~rbuffer();


	int		getBufferLen();
	void	write(void *data, int len);
	int		read(void *data, int len);
	void	clearBuffer();
	void	copy(rbuffer *);
private:
	int getBufferType();
private:
	unsigned char *begin;
	unsigned char *end;
	unsigned char *head;
	unsigned char *tail;
};

