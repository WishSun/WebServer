/*************************************************************************
	> File Name: parse_configure_file.c
	> Author: WishSun
	> Mail: WishSun_Cn@163.com
	> Created Time: 2018年04月15日 星期日 12时58分50秒
 ************************************************************************/

/* 解析程序配置文件-----------------------------------------*/

#include <libconfig.h>
#include <stdint.h>
#include <malloc.h>
#include <string.h>

#include "../../inc/parse_configure_file.h"


/* 操作配置文件的全局句柄*/
config_t  *p_conf = NULL;

/* 在配置文件中获取单值变量
 * @path : 变量在配置文件中位置
 * @val  : 用来存储取到的变量的值
 * @value_type : 该单值变量的类型
 */
int get_val_single( const char *path, void *val, int value_type )
{
    if( ! p_conf || !path || !val )
    {
        printf( "input arguments error!\n" );
        return -1;
    }

    switch( value_type )
    {
        case TYPE_INT:  
        {
            if( config_lookup_int( p_conf, path, (int *)val ) == 0 )
            {
                printf( "parse int value error!\n" );
                return -1;
            }
            break;
        }
        case TYPE_LONG:
        {
            if( config_lookup_int64( p_conf, path, (long long int *)val ) == 0 )
            {
                printf( "parse int64 value error!\n" );
                return -1;
            }
            break;
        }
        case TYPE_DOUBLE:
        {
            if( config_lookup_float( p_conf, path, (double *)val ) == 0 )
            {
                printf( "parse double value error!\n" );
                return -1;
            }
            break;
        }
        case TYPE_STRING:
        {
            char *ptr = NULL;

            if( config_lookup_string( p_conf, path, (const char**)&ptr ) == 0 )
            {
                printf( "parse string value error!\n" );
                return -1;
            }
            strcpy( (char *)val, ptr );
            break;
        }
        default:
        {
            printf( "arguments value type error!\n" );
            return -1;
        }
    }

    return 0;
}


/* 在配置文件中获取数组变量
 * @path : 变量在配置文件中位置
 * @val  : 二级指针指向一个数组，用来存放数组内容
 * @value_type : 该数组变量的类型
 */
int get_val_array( const char *path, void **val, int count, int value_type )
{
    if( !p_conf || !path || !val )
    {
        printf( "input arguments error!\n" );
        return -1;
    }

    int num = -1, i;

    /* 集合句柄*/
    config_setting_t  *p_set;         

    /* 获取数组或列表集合*/
    p_set = config_lookup( p_conf, path );
    if( p_set == NULL )
    {
        printf( "parse arguments: [%s] error!\n", path );
        return -1;
    }

    /* 获取集合的长度*/
    num = config_setting_length( p_set );
    if( count > 0 && num != count )
    {
        printf( "list or array elements' number is error!\n" );
        return -1;
    }

    /* 根据数组的类型，分别取出数组中的每一个元素*/
    switch( value_type )
    {
        case TYPE_INT:
        {
            int **ptr = (int **)val;
            for ( i = 0; i < num; i++ )
            {
                (*ptr)[i] = config_setting_get_int_elem( p_set, i );
            }
            break;
        }
        case TYPE_LONG:
        {
            long long int **ptr = (long long int **)val;
            for ( i = 0; i < num; i++ )
            {
                (*ptr)[i] = config_setting_get_int64_elem( p_set, i );
            }
            break;
        }
        case TYPE_DOUBLE:
        {
            double **ptr = (double **)val;
            for ( i = 0; i < num; i++ )
            {
                (*ptr)[i] = config_setting_get_float_elem( p_set, i );
            }
            break;
        }
        case TYPE_STRING:
        {
            for ( i = 0; i < num; i++ )
            {
                ((char **)val)[i] = (char *)malloc( 256 );
                strcpy( ((char **)val)[i], config_setting_get_string_elem( p_set, i ) );
            }
            break;
        }
        default:
        {
            printf( "list or array type error!\n" );
            return -1;
        }
    }

    return 0;
}

/* 初始化并打开配置文件
 * @filename: 配置文件绝对路径名
 */
int open_conf( const char *filename )
{
    if( ! filename )
    {
        printf( "config filename error\n" );
        return -1;
    }

    /* 申请句柄资源*/
    p_conf = (config_t *)malloc(sizeof(config_t));
    if( !p_conf )
    {
        printf("malloc config error!\n");
        return -1;
    }

    /* 初始化libconfig*/
    config_init(p_conf);

    /* 打开配置文件，并与libconfig、句柄关联*/
    if( config_read_file(p_conf, filename) == 0)
    {
        printf( "config read file error:[%s]!\n", filename );
        return -1;
    }

    return 0;
}

/* 关闭配置文件，并释放资源
 */
void close_conf()
{
    config_destroy(p_conf);
    free(p_conf);
    p_conf = NULL;
}

