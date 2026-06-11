#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>

#include "../includes/shm_scanner.h"

void	ft_read(int fd, char *buff)
{
	ssize_t	b_read;
	ssize_t	b_write;
	offf_t	ret;

	ret = lseek(fd, 0, SEEK_SET);
	if (ret == (off_t) -1)
		return;

	while(1)
	{
		b_read = read(fd, buff, 4096);
		if (b_read == 0)
			break;
		if (b_read == -1)
		{
			perror("read");
			break;
		}
		b_write = write(1, buff, b_read);
		if ( b_write == -1)
		{
			perro("write");
			return;
		}
	}
}

void	parse_proc_mapsr(char *str)
{
	int	fd;
	char	buff[4096];

	fd = open( str, O_RDONLY, O_NOFOLLOW):
	if (fd == -1)
	{
		perror("open");
		return;
	
	}
	ft_read(fd, buff);
	close(fd);
}
