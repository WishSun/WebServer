/*************************************************************************
	> File Name: parse_configure_file.h
	> Author: WishSun
	> Mail: WishSun_Cn@163.com
	> Created Time: 2018年04月15日 星期日 13时00分04秒
 ************************************************************************/

/* 解析程序配置文件-----------------------------------------*/
#ifndef _PARSE_CONFIGURE_FILE_H
#define _PARSE_CONFIGURE_FILE_H

/* 单值类型枚举声明*/
enum __value_type_t
{
    TYPE_INT = 0,
    TYPE_LONG,
    TYPE_DOUBLE,
    TYPE_STRING
};

/* 在配置文件中获取单值变量
 * @path : 变量在配置文件中位置 例: base.disk_num
 * @val  : 用来存储取到的变量的值
 * @value_type : 该单值变量的类型
 */
int get_val_single( const char *path, void *val, int value_type );


/* 在配置文件中获取数组变量
 * @path : 变量在配置文件中位置
 * @val  : 二级指针指向一个数组，用来存放数组内容
 * @value_type : 该数组变量的类型
 */
int get_val_array( const char *path, void **val, int count, int value_type );

/* 初始化并打开配置文件
 * @filename: 配置文件绝对路径名
 */
int open_conf( const char *name );

/* 关闭配置文件，并释放资源
 */
void close_conf();
#endif
