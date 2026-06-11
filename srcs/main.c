#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include "../includes/shm_scanner.h"

void	ft_catcher(t_context **content, void *value, char *ar)
{
	t_context	*ctx;

	ctx = *content;
	if ((content == NULL) && (*content == NULL) && (ar == NULL))
		return;
	if (ft_findstr(ar,"--target") == 0)
	{
		ctx->pid = atoi((char *)value);
		printf("ok target\n");
	}
	if (ft_findstr(ar,"--pattern") == 0)
	{
		//ctx->pattern = (char)malloc(sizeof(char));
		ctx->pattern = (char *)value;
		printf("ok pattern \n");
	}
	if (ft_findstr(ar,"--thread") == 0)
	{

		ctx->num_threads = atoi((char *)value);
		printf("ok thread\n");
	}
	if (ft_findstr(ar,"--filter") == 0)
	{
		ctx->filter_perms = (char *) value;
		printf("ok filter\n");
	}
}

ssize_t	verifier(t_context *content)
{
	if (!content->pid)
	{
		return (-1);
	}
	if (!content->pattern)
	{
		return (-1);
	}
	return (0);
}

int	main(int arc, char **arv)
{
	char	*arg_error;
	size_t	i;
	size_t	j;
	ssize_t	status;
	char	*table[4];
	t_context	*context_param;

	arg_error = "invalide arguments please verify your argument parameter";
	i = 1;
	context_param = (t_context *) malloc(sizeof(t_context));
	context_param->is_hex = 1;
	table[0] = "--target";
	table[1] = "--pattern";
	table[2] = "--thread";
	table[3] = "--filter";
	ft_print_form();
	if (arc == 10){
		while (*(arv + i)){
			if ( i < (size_t) arc){
				j = 0;
				while (j < 5){
					//printf("value de i est %s\n", *(arv + i));
					status = ft_findstr(*(arv + i), "--hex");
					if (status == 0){
						context_param->is_hex = 0;
						i -= 1;
						printf("ok hex\n");
						break;
					}
				
					else{
						status = ft_findstr(*(arv + i), *(table + j));
						if (status == 0){
							//printf(" %s --> %s  \n",*(table + j),*(arv + (i + 1)));
							ft_catcher(&context_param, *(arv + (i + 1)), table[j]);
							break;
						}
					}
					j++;
				}	
				i += 2;
			}
			else
				break;
		}
	}
	else{
		printf("%s\n", arg_error);
		return (1);
	}
	printf("successfully \n");
	//ft_free_table(table);
	return (0);
}
