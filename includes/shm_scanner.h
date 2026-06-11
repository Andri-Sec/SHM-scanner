#ifndef SHM_SCANNER_H
# define SHM_SCANNER_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/types.h>

typedef struct s_segment
{
	unsigned long	start;
	unsigned long	end;
	char		perms[5];
	char		name[256];
}	t_segment;

typedef struct	s_task
{
	t_segment	segment;
	unsigned char	*pattern;
	size_t		pattern_len;
	int		is_hex;
}	t_task;

typedef struct	s_match
{
	unsigned long	address;
	int		pid;
	char		segment_name[256];
}	t_match;

typedef struct	s_task_queue
{
	t_task	*task;
	size_t	size;
	size_t	index;
	pthread_mutex_t	mutex;
	pthread_cond_t	cond;
}	t_task_queue;

typedef struct	s_context
{
	int	pid;
	char	*pattern;
	int	is_hex;
	int	num_threads;
	char	*filter_perms;
	t_match	*results;
	size_t	result_count;
	pthread_mutex_t	result_mutex;
}	t_context;

// utils.c
size_t	ft_strlen(char *str);
void	ft_print_form(void);
ssize_t	ft_findstr(char *s1, char *s2);
int	ft_atoi(char *str);
//parser.c
void	parser(char *str);
void	ft_read(int fd, char *buff);
#endif
