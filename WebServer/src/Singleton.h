/*************************************************************************
	> File Name: Singleton.h
	> Author: WishSun
	> Mail: WishSun_Cn@163.com
	> Created Time: 2018年09月09日 星期日 01时53分33秒
 ************************************************************************/

#ifndef _SINGLETON_H
#define _SINGLETON_H

template< typename T > 
class Singleton
{
private:
    static T* m_instance;

    Singleton()
    {
        
    }
public:
    static T* GetInstance()
    {
        if( m_instance == NULL )
        {
            m_instance = new T;
        }

        return m_instance;
    }
    ~Singleton()
    {
        if( m_instance != NULL )
        {
            delete m_instance;
            m_instance = NULL;
        }
    }
};

template < typename T >
T * Singleton< T >::m_instance = NULL;


#endif
