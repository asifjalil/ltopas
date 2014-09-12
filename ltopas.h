#ifndef _LTOPAS_H
#define _LTOPAS_H

#define DEVMAP_DIR  "/dev/mapper"        
#define DEVMAP_MAJOR  253                
#define MAX_FILE_LEN   256               
#define MAX_NAME_LEN 72                  
                                         
/*  macro to print pgpgin/s, pgpgout/s, pgfault/s, pgmajfault/s */
#define PER_SEC_VM(n,m,p)  ((((double) ((n) - (m))) / (p)))       
#define PER_SEC(x,y) (1000.0 * (x) / (y))
#define MAX_PARTITIONS 1024              
                                         
#ifndef TRUE                             
#define TRUE 1                           
#endif                                   
                                         
#ifndef FALSE                            
#define FALSE 0                          
#endif                                   

#ifndef boolean      
typedef int boolean;
#endif                


#endif  /* _LTOPAS_H */
