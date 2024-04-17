#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <dirent.h>
#include <signal.h>
#include <sys/stat.h>

#define PATH_MAX_LENGTH 1024

pid_t pid;
int timeout = 0;
int contextError = 0;
int wrong = 0;

//커맨드 입력 방법 출력
void print_usage()
{
    fprintf(stderr, "Usage: ./autojudge -i <inputdir> -a <outputdir> -t <timelimit> <target src>\n");
}

//주어진 소스코드를 컴파일하여 실행 파일을 생성하는 함수
char *compile_target(char *target_src)
{
    int status;
    char *target_exe = "./target";

    //자식 프로세스를 생성
    pid = fork();
    if (pid == 0) //자식 프로세스가 실행중이라면
    {
        execlp("gcc", "gcc", "-fsanitize=address", "-o", target_exe, target_src, NULL);
        exit(0);
    }
    else
    {
        wait(&status); //자식 프로세스가 종료될 때까지 기다림
        if (status != 0)
        {
            fprintf(stderr, "Failed to compile target program\n");
            return NULL;
        }
    }
    return target_exe;
}

//파일 경로를 동적으로 할당해 설정하는 함수
char *construct_file_path(const char *dir_path, const char *file_name)
{
    char *file_path = (char *)malloc(PATH_MAX_LENGTH);
    if (file_path == NULL)
    {
        perror("Failed to allocate memory for file path");
        exit(EXIT_FAILURE);
    }

    int ret = snprintf(file_path, PATH_MAX_LENGTH, "%s/%s", dir_path, file_name);
    if (ret >= PATH_MAX_LENGTH || ret < 0)
    {
        fprintf(stderr, "Failed to construct file path\n");
        exit(EXIT_FAILURE);
    }

    return file_path;
}
//주어진 경로에 대해 디렉토리를 생성하는 함수
void create_directory(const char *dir_path)
{
    if (mkdir(dir_path, 0777) == -1)
    {
        if (errno != EEXIST)
        {
            perror("Failed to create directory");
            exit(EXIT_FAILURE);
        }
    }
}

//시간 초과 시 자식 프로세스를 종료하는 함수
void handle_timeout(int sig)
{
    kill(pid, SIGKILL);
    timeout++;
}

//두 파일의 내용을 비교해 동일한 내용인지 확인하는 함수
int compare_files(const char *file1_path, const char *file2_path)
{
    FILE *file1 = fopen(file1_path, "r");
    FILE *file2 = fopen(file2_path, "r");

    if (file1 == NULL || file2 == NULL)
    {
        perror("Failed to open file");
        return -1;
    }

    int ch1, ch2;

    do
    {
        ch1 = fgetc(file1);
        ch2 = fgetc(file2);
        if (ch1 != ch2) //두 파일에서 읽은 문자가 다른 경우, 두 파일을 닫고 -1 반환
        {
            fclose(file1);
            fclose(file2);
            return -1;
        }
    } while (ch1 != EOF && ch2 != EOF); //이는 두 파일이 동일하다는 것을 의미

    fclose(file1);
    fclose(file2);
    return 0;
}

//테스트를 실행하고 결과를 평가하는 함수
int run_test(char *input_dir, char *answer_dir, char *output_dir, char *target_exe, int *correct, int *timeout, int *wrong, int timeout_limit)
{
    DIR *input_dp, *answer_dp;
    struct dirent *input_entry, *answer_entry;
    struct timeval start, end;
    int status;
    long elapsed_time;
    int buffer = 0;

    //입력 디렉토리 열기
    input_dp = opendir(input_dir);
    if (!input_dp)
    {
        perror("Failed to open input directory");
        return 1;
    }
    //정답 디렉토리 열기
    answer_dp = opendir(answer_dir);
    if (!answer_dp)
    {
        perror("Failed to open answer directory");
        closedir(input_dp);
        return 1;
    }
    //출력 디렉토리 생성
    create_directory(output_dir);

    while ((input_entry = readdir(input_dp)) != NULL)
       // 각 파일 확인
    {
        if (strstr(input_entry->d_name, ".txt"))
        {
            char *input_filename = strdup(input_entry->d_name);

            char *input_path = construct_file_path(input_dir, input_filename);
            char *output_path = construct_file_path(output_dir, input_filename);

            //SIGALRM 신호에 대한 시그널 핸들러 설정
            signal(SIGALRM, handle_timeout);

            //파이프 생성
            int pipefd[2];
            if (pipe(pipefd) == -1)
            {
                perror("pipe");
                exit(EXIT_FAILURE);
            }
            //자식 프로세스 생성
            pid = fork();
            //자식 프로세스 실행중일 때
            if (pid == 0)
            {
                close(pipefd[0]);

                int input_fd = open(input_path, O_RDONLY);
                // 내용이 자식 프로세스의 표준 입력으로 리디렉션
                if (input_fd == -1)
                {
                    perror("open");
                    exit(EXIT_FAILURE);
                }

                dup2(input_fd, STDIN_FILENO);
                // 자식 프로세스의 출력이 파이프의 한쪽 끝으로 리디렉션
                // 이 함수를 통해서 프로세스가 표준 입력을 input_fd에서 읽을 수 있도록 만들어 사용자 입력 대신 input 파일 경로를 자동으로 전달
                close(input_fd);

                dup2(pipefd[1], STDOUT_FILENO);
                // pipefd[1] 을 stdout_fileno에 복제해서 프로세스가 파이프를 통해 데이터를 사용할 수 있도록
                // 즉, 프로세스의 출력이 pipe_fd[1]을 통해 파이프에 쓰여짐

                close(pipefd[1]);

                execl(target_exe, target_exe, NULL);
                exit(EXIT_FAILURE);
            }
            //부모 프로세스 실행중일 때
            else if (pid > 0)
            {
                close(pipefd[1]);

                gettimeofday(&start, NULL);
                alarm(timeout_limit);

                int output_fd = open(output_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
                // 부모 프로세스는 출력 파일을 열어서(open) 파이프에서 읽은 내용을 해당 파일에 쓰기
                if (output_fd == -1)
                {
                    perror("open");
                    exit(EXIT_FAILURE);
                }

                char buf;
                ssize_t bytes_read;
                while ((bytes_read = read(pipefd[0], &buf, sizeof(buf))) > 0)
                {
                    if (write(output_fd, &buf, bytes_read) != bytes_read)
                    {
                        perror("write");
                        exit(EXIT_FAILURE);
                    }
                }
                if (bytes_read == -1)
                {
                    perror("read");
                    exit(EXIT_FAILURE);
                }

                close(pipefd[0]);
                close(output_fd);

                int res = waitpid(pid, &status, 0);

                gettimeofday(&end, NULL);
                elapsed_time = (end.tv_sec - start.tv_sec) * 1000 + (end.tv_usec - start.tv_usec) / 1000;
                //실행시간 계산

                if (WIFEXITED(status))
                {
                    int exit_status = WEXITSTATUS(status);
                    if (exit_status == 0)
                    {
                        if (compare_files(output_path, construct_file_path(answer_dir, input_filename)) == 0)
                        {
                            fprintf(stderr, "Test %s: Correct\n", input_filename);
                            fprintf(stderr, " - Time : %ld m/s \n", elapsed_time);
                            (*correct)++;
                        }
                        else
                        {
                            fprintf(stderr, "Test %s: Wrong Answer\n", input_filename);
                            (*wrong)++;
                        }
                    }
                    else
                    {
                        fprintf(stderr, "Test %s: Context Error\n", input_filename);
                        contextError++;
                    }
                }
                else
                {
                    if (buffer < *timeout)
                    {
                        fprintf(stderr, "Test %s: Runtime Error\n", input_filename);
                        buffer++;
                    }
                    else
                    {
                        fprintf(stderr, "Test %s: Runtime Error\n", input_filename);
                        (*wrong)++;
                    }
                }
            }
            else
            {
                perror("fork");
                exit(EXIT_FAILURE);
            }

            free(input_filename);
            free(input_path);
            free(output_path);
        }
    }

    closedir(input_dp);
    closedir(answer_dp);
    return 0;
}

void report_results(int correct, int timeout, int wrong, int contextError, long total_time)
{
    printf("Correct: %d\n", correct);
    printf("Timeout: %d\n", timeout);
    printf("Wrong Answer: %d\n", wrong);
    printf("Context Error: %d\n", contextError);
}

int main(int argc, char *argv[])
{
    char *input_dir = NULL;
    char *answer_dir = NULL;
    char *target_src = NULL;
    int opt;
    int correct = 0;
    long total_time = 0;
    int timeout_limit = 0;
    //명령행 옵션을 처리하고 파일 경로 가져오기

    while ((opt = getopt(argc, argv, "i:a:t:")) != -1)
    {
        switch (opt)
        {
        case 'i':
            input_dir = optarg;
            break;
        case 'a':
            answer_dir = optarg;
            break;
        case 't':
            timeout_limit = atoi(optarg);
            break;
        default:
            print_usage();
            return 1;
        }
    }
    //올바른 인자가 주어지지 않을 경우 command line 출력
    if (!input_dir || !answer_dir || !timeout_limit || optind >= argc)
    {
        print_usage();
        return 1;
    }

    target_src = argv[optind];

    char *output_dir = "./output";

    //대상 프로그램 컴파일
    char *target_exe = compile_target(target_src);
    if (!target_exe)
    {
        return 1;
    }
    //테스트 실행 및 평가
    if (run_test(input_dir, answer_dir, output_dir, target_exe, &correct, &timeout, &wrong, timeout_limit) != 0)
    {
        return 1;
    }
    //최종 결과 출력
    report_results(correct, timeout, wrong, contextError, total_time);

    return 0;
}