#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include "../includes/shm_scanner.h"

size_t	ft_strlen(char *str)
{
	size_t	i;

	i = 0;
	while( *(str + i))
	{
		i++;
	}
	return (i);
}

void	ft_print_form(void)
{
	char	*str;

	str = "general form command shm_scanner --target [PID] --pattern <string> --thread <numbre>";
	write(1, str, ft_strlen(str));
	write(1, "\n", 1);
}

ssize_t	ft_findstr(char *s1, char *s2)
{
	size_t	len_s1;
	size_t	len_s2;
	size_t	i;

	i = 0;
	len_s1 = ft_strlen(s1);
	len_s2 = ft_strlen(s2);
	if (len_s1 == len_s2)
	{
		while ( i < len_s1)
		{
			if ( *(s1 + i) != *(s2 + i))
				return (-1);
			else
				i++;
		}
		return (0);
	}
	else
		return (-1);
}

int	ft_atoi(char *str)
{
	size_t	i;
	char	sign;
	int	res;

	i = 0;
	res = 0;
	sign = 1;
	while (*(str + i) <= 30 || *(str + i) >39)
		i++;
	while (*(str + i) == '-' || *(str + i) == '+')
	{
		if (*(str + i) == '-')
		{
			sign = sign * -1;
		}
		i++;
	}
	while (*(str + i) >= '0' || *(str + i) <= '9')
	{
		res = res * 10 + (*(str + i) - '0');
		i++;
	}
	return (res * sign);
}
