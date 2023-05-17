/*
 * mm-naive.c - The fastest, least memory-efficient malloc package.
 * 
 * In this naive approach, a block is allocated by simply incrementing
 * the brk pointer.  A block is pure payload. There are no headers or
 * footers.  Blocks are never coalesced or reused. Realloc is
 * implemented directly using mm_malloc and mm_free.
 *
 * NOTE TO STUDENTS: Replace this header comment with your own header
 * comment that gives a high level description of your solution.
 */
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>

#include "mm.h"
#include "memlib.h"

/*********************************************************
 * NOTE TO STUDENTS: Before you do anything else, please
 * provide your team information in the following struct.
 ********************************************************/
team_t team = {
    /* Team name */
    "ateam",
    /* First member's full name */
    "Harry Bovik",
    /* First member's email address */
    "bovik@cs.cmu.edu",
    /* Second member's full name (leave blank if none) */
    "",
    /* Second member's email address (leave blank if none) */
    ""

};

/* single word (4) or double word (8) alignment */
#define ALIGNMENT 8

/* rounds up to the nearest multiple of ALIGNMENT */
#define ALIGN(size) (((size) + (ALIGNMENT-1)) & ~0x7)


#define SIZE_T_SIZE (ALIGN(sizeof(size_t)))

/*global variable & functions*/
static char* heap_listp;

void *mm_realloc(void *ptr, size_t size);
int mm_init(void);
static void *extend_heap(size_t words);
void *mm_malloc(size_t size);
static void *find_fit(size_t asize);
static void place(void *bp, size_t asize);
void mm_free(void *bp);
static void *coalesce(void *bp);

#define NEXT_FIT // NEXT_FIT 선언
#ifdef NEXT_FIT
    static char* last_freep;
#endif

/* 기본 상수와 매크로*/
#define WSIZE 4 // 기본 단위인 word 선언
#define DSIZE 8 // 기본 단위인 double word 선언
#define CHUNKSIZE (1<<12) // 새로 할당받는 힙의 크기 CHUNKSIZE를 정의 4096byte 만큼

#define MAX(x,y) ((x)>(y)? (x):(y)) // 최대값 구하는 매크로

/*header 및 footer 값(size + allocated) 리턴*/
#define PACK(size, alloc) ((size) | (alloc)) // 이곳에 0(freed), 1(allocated) flag를 삽입

/*주소 p에서의 word를 읽어오거나 쓰는 함수*/
#define GET(p) (*(unsigned int *)(p)) // 포인터 p가 가리키는 곳의 값을 리턴하거나 val을 저장
#define PUT(p,val) (*(unsigned int *)(p)=(val))

/*header or footer 에서 블록의 size,allocated field를 읽어온다.*/
#define GET_SIZE(p) (GET(p) & ~0x7)
#define GET_ALLOC(p) (GET(p) & 0x1)

/*블록 포인터 bp를 인자로 받아 블록의 header와 footer의 주소를 반환한다.*/
#define HDRP(bp) ((char *)(bp) - WSIZE) // wise 4를 뺀다는 것은 주소가 4byte(1 word) 뒤로 간다는 뜻, bp의 1word 뒤는 헤더
#define FTRP(bp) ((char *)(bp) + GET_SIZE(HDRP(bp))-DSIZE)

/*블록 포인터 bp를 인자로 받아 이후, 이전 블록의 주소를 리턴한다.*/
#define NEXT_BLKP(bp) ((char *)(bp)+ GET_SIZE(((char *)(bp)-WSIZE))) // (char*)(bp) + GET_SIZE(지금 블록의 헤더값)
#define PREV_BLKP(bp) ((char *)(bp)- GET_SIZE(((char *)(bp)-DSIZE))) // (char*)(bp) - GET_SIZE(이전 블록의 풋터값)


/* 
 * mm_init - initialize the malloc package.
 */
int mm_init(void)//최초 가용 블록으로 힙 생성하기
{
    if((heap_listp= mem_sbrk(4*WSIZE))==(void *)-1){
        return -1;
    }
    PUT(heap_listp,0); //더블 워드 경계로 정렬된 미사용 패딩
    PUT(heap_listp + (1*WSIZE), PACK(DSIZE,1)); // prologue header
    PUT(heap_listp + (2*WSIZE), PACK(DSIZE,1)); // prologue footer
    PUT(heap_listp + (3*WSIZE), PACK(0,1)); // epliougue header
    heap_listp += (2*WSIZE); // 정적 전역 변수는 늘 prologue block을 가리킨다.

    #ifdef NEXT_FIT
        last_freep = heap_listp; //힙의 시작지점, 프롤로그를 가리키는 포인터 heap_listp를 가리키게 한다.
    #endif

    /*그 후 CHUNKSIZE만큼 힙을 확장해 초기 가용 블록을 생성한다.*/
    if (extend_heap(CHUNKSIZE/WSIZE)==NULL)
    {
        return -1;
    } // 실패하면 -1 리턴

    return 0;
}

static void *extend_heap(size_t words){ // extend_heap 새 가용 블록으로 힙 확장하기

    char *bp;
    size_t size;

    /*더블 워드 정렬에 따라 메모리를 mem_sbrk 함수를 이용해 할당받는다.*/
    size = (words%2) ? (words +1) * WSIZE : words * WSIZE; //size를 짝수 word && byte 형태로 만든다.
    if((long)(bp=mem_sbrk(size))== -1){ // 새 메모리의 첫부분을 bp로 둔다. 주소값은 int로 못받아서 long으로 cating
        return NULL;

    }
    /*새 가용 블록의 header와 footer를 정해주고 epilgue block을 가용 블록 맨 끝으로 옮긴다.*/
    PUT(HDRP(bp), PACK(size,0)); // header, 할당 안 해줬으므로 0
    PUT(FTRP(bp),PACK(size,0)); // footer
    PUT(HDRP(NEXT_BLKP(bp)),PACK(0,1)); // 새 에필로그 헤더

    /*만약 이진 블록이 가용 블록이라면 연결시킨다.*/
    return coalesce(bp);

}
/* 
 * mm_malloc - Allocate a block by incrementing the brk pointer.
 *     Always allocate a block whose size is a multiple of the alignment.
 */
// mm_malloc 가용 리스트에서 블록 할당하기
void *mm_malloc(size_t size)
{   
    size_t asize;
    size_t extendsize;
    char* bp;

    //가짜 요청 spurious request 무시
    if (size==0){
        return NULL;
    }
    //16byte에 맞춰서 size 정해줌
    //이때 size가 16보다 적으면 asize를 16으로 정해줌
    if(size <= DSIZE){
        asize = 2*DSIZE;
    }
    else{//size가 16보다 크면 asize를 8의 배수로 정해줌
        asize = DSIZE * ((size + (DSIZE)+(DSIZE-1))/DSIZE);
    }

    /*할당할 가용 리스트를 찾아 필요하다면 분할해서 할당한다.*/
    if ((bp=find_fit(asize))!=NULL){
        place(bp,asize); //필요하다면 분할하여 할당한다.
        return bp;
    }
    //만약 맞는 크기의 가용 블록이 없다면 새로 힙을 늘려서 새 힙에 메모리를 할당한다.
    extendsize = MAX(asize,CHUNKSIZE); // 둘 중 더 큰 값으로 사이즈를 정한다.
    if((bp = extend_heap(extendsize/WSIZE))==NULL){
        return NULL;
    }
    place(bp,asize); //필요하다면 분할하여 할당 
    return bp;

}

static void *find_fit(size_t asize){
    /*next fit*/
    #ifdef NEXT_FIT
        void* bp;
        void* old_last_freep = last_freep; //old_last_freep에 last_freep를 저장한다.

        //이전 탐색이 종료된 시점에서부터 다시 시작
        //연결할 때 만약 직전의 가용블록과 연결되었을 경우 last_freep 역시 직전 블록의 블록 포인터를 가리켜야 한다.
        for(bp=last_freep;GET_SIZE(HDRP(bp)); bp=NEXT_BLKP(bp)){
            if (!GET_ALLOC(HDRP(bp))&& (asize <= GET_SIZE(HDRP(bp)))){
                return bp;
            }
        }
        /*만약 끝까지 찾았는데도 안나왔으면 처음부터 찾아본다. 맨앞에서 부터도 탐색하는 것이 효율적이다.*/ 

        for(bp = heap_listp; bp < old_last_freep; bp = NEXT_BLKP(bp)){
            if(!GET_ALLOC(HDRP(bp))&& (asize <= GET_SIZE(HDRP(bp)))){
                return bp;
            }
        }
        last_freep = bp; //bp를 last_freep로 돌려준다.
        return NULL;
    #else
        /*First fit*/
        void *bp;

        //heap list의 맨 뒤는 프롤로그 블록이다. heap list에서 유일하게 할당된 블록이므로 얘를 만나면 탐색 종료
        for ( bp= heap_listp; GET_SIZE(HDRP(bp))>0; bp=NEXT_BLKP(bp)){
            if(!GET_ALLOC(HDRP(bp))&& (asize <= GET_SIZE(HDRP(bp)))){
                return bp;
            }
        }
        

        return NULL;
    #endif
}

static void place(void *bp, size_t asize){ //효율성있게 분할 하기 위해 사용 
    //현재 할당할 수 있는 후보 가용 블록의 주소
    size_t csize = GET_SIZE(HDRP(bp));

    //분할이 가능한 경우
    if ((csize - asize) >= (2*DSIZE)){ // 16바이트 보다 클경우
        //앞의 블록은 할당 블록으로
        PUT(HDRP(bp),PACK(asize,1));
        PUT(FTRP(bp),PACK(asize,1));
        //뒤의 블록은 가용 블록으로 분할한다.
        bp = NEXT_BLKP(bp);
        PUT(HDRP(bp),PACK(csize-asize,0));//나머지 블록은 가용블록으로 설정
        PUT(FTRP(bp),PACK(csize -asize,0));//나머지 블록은 가용블록으로 설정

    }
    else{//분할이 가능하지 않은 경우 16바이트 보다 작을경우
        PUT(HDRP(bp),PACK(csize,1)); //그냥 할당 블록으로 설정
        PUT(FTRP(bp), PACK(csize,1));
    }
}

/*
 * mm_free - Freeing a block does nothing.
 */
void mm_free(void *bp)
{
    //해당 블록의 size를 알아내 header와 footer의 정보를 수정한다.
    size_t size = GET_SIZE(HDRP(bp));
    //header와 footer를 설정
    PUT(HDRP(bp),PACK(size,0)); // 할당을 해제해준다.
    PUT(FTRP(bp),PACK(size,0)); // 할당을 해제해준다.

    //만약 앞뒤의 블록이 가용 상태라면 연결한다.
    coalesce(bp);
}
/* 해당 가용 블록을 앞뒤 가용 블록과 연결하고 연결된 가용 블록의 주소를 리턴한다.*/
static void *coalesce(void *bp)
{   //직전 블록의 footer, 직후 블록의 header를 보고 가용 여부를 확인
    size_t prev_alloc = GET_ALLOC(FTRP(PREV_BLKP(bp))); // 직전 블록 가용여부
    size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp))); // 직후 블록 가용 여부
    size_t size = GET_SIZE(HDRP(bp)); // header 사이즈 크기를 가져옴

    //case 1 : 직전, 직후 블록이 모두 할당 -> 해당 블록만 free list에 넣어줌
    if(prev_alloc && next_alloc){
        return bp;
    }

    //case 2: 직전 블록 할당, 직후 블록 가용
    else if (prev_alloc && !next_alloc){
        size+= GET_SIZE(HDRP(NEXT_BLKP(bp))); //직후 블록의 사이즈를 기존 header 사이즈 크기에 더해줌
        PUT(HDRP(bp),PACK(size,0)); // header 를 해제한다.
        PUT(FTRP(bp),PACK(size,0)); // footer 를 해제한다.

    }

    //case 3: 직전 블록 가용, 직후 블록 할당
    else if(!prev_alloc && next_alloc){
        size+= GET_SIZE(HDRP(PREV_BLKP(bp))); //직전 블록의 헤더 사이즈를 기존 header 사이즈 크기에 더해줌
        PUT(FTRP(bp),PACK(size,0)); //footer를 해제한다. 
        PUT(HDRP(PREV_BLKP(bp)),PACK(size,0)); //직전 header를 해제한다.
        bp= PREV_BLKP(bp); // 블록 포인터를 직전 블록으로 옮긴다.

    }

    //case 4 : 직전, 직후 블록 모두 가용
    else{
        size += GET_SIZE(HDRP(PREV_BLKP(bp)))+GET_SIZE(FTRP(NEXT_BLKP(bp))); //직전 블록의 헤더 사이즈와 직후 블록의 헤더 사이즈를 더한 크기
        PUT(HDRP(PREV_BLKP(bp)),PACK(size,0)); //직전 헤더를 free한다.(0으로 할당해줌)
        PUT(FTRP(NEXT_BLKP(bp)),PACK(size,0)); //직후 footer를 free한다.(0으로 할당해줌)
        bp = PREV_BLKP(bp); //블록 포인터를 직전 블록으로 옮긴다.
    }
    #ifdef NEXT_FIT
        last_freep = bp;
    #endif
    return bp;

}

/*
 * mm_realloc - Implemented simply in terms of mm_malloc and mm_free
 */

void *mm_realloc(void *ptr, size_t size)
{
    void *oldptr = ptr;
    void *newptr;
    size_t copySize;
    
    newptr = mm_malloc(size);
    if (newptr == NULL)
      return NULL;
    copySize = GET_SIZE(HDRP(oldptr));
    // copySize = *(size_t *)((char *)oldptr - SIZE_T_SIZE);
    if (size < copySize)
      copySize = size;
    memcpy(newptr, oldptr, copySize);
    mm_free(oldptr);
    return newptr;
}














