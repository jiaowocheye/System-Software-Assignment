#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include "dplist.h"

/*
 * definition of error codes
 * */
#define DPLIST_NO_ERROR 0
#define DPLIST_MEMORY_ERROR 1 // error due to mem alloc failure
#define DPLIST_INVALID_ERROR 2 //error due to a list operation applied on a NULL list

#ifdef DEBUG
#define DEBUG_PRINTF(...) 									         \
		do {											         \
			fprintf(stderr,"\nIn %s - function %s at line %d: ", __FILE__, __func__, __LINE__);	 \
			fprintf(stderr,__VA_ARGS__);								 \
			fflush(stderr);                                                                          \
                } while(0)
#else
#define DEBUG_PRINTF(...) (void)0
#endif


#define DPLIST_ERR_HANDLER(condition,err_code)\
	do {						            \
            if ((condition)) DEBUG_PRINTF(#condition " failed\n");    \
            assert(!(condition));                                    \
        } while(0)


/*
 * The real definition of struct list / struct node
 */

struct dplist_node {
    dplist_node_t * prev, * next;
    void * element;
};

struct dplist {
    dplist_node_t * head;
    void * (*element_copy)(void * src_element);
    void (*element_free)(void ** element);
    int (*element_compare)(void * x, void * y);
};


dplist_t * dpl_create (// callback functions
        void * (*element_copy)(void * src_element),
        void (*element_free)(void ** element),
        int (*element_compare)(void * x, void * y)
)
{
    dplist_t * list;
    list = malloc(sizeof(struct dplist));
    DPLIST_ERR_HANDLER(list==NULL,DPLIST_MEMORY_ERROR);
    list->head = NULL;
    list->element_copy = element_copy;
    list->element_free = element_free;
    list->element_compare = element_compare;
    return list;
}

void dpl_free(dplist_t ** list, bool free_element)
{
    DPLIST_ERR_HANDLER(list==NULL,DPLIST_INVALID_ERROR);

    int length=dpl_size((*list));

    for(int index=length-1; index>=0; index--)
    {
        dpl_remove_at_index((*list), index, free_element);
    }

    free(*list);
    *list=NULL;
}

dplist_t * dpl_insert_at_index(dplist_t * list, void * element, int index, bool insert_copy)
{
    int size=sizeof(dplist_node_t);
    dplist_node_t *list_node=(dplist_node_t *)(malloc(size));
    dplist_node_t *ref_at_index;
    DPLIST_ERR_HANDLER(list_node==NULL,DPLIST_MEMORY_ERROR);
    // pointer drawing breakpoint
    if (list->head == NULL)
    { //list_size==0
        list_node->prev = NULL;
        list_node->next = NULL;
        list->head = list_node;
        // pointer drawing breakpoint
    } else if (index <= 0)
    {
        //make the input node as the first node of the list
        list_node->prev = NULL;             //it is the first node
        list_node->next = list->head;       //previous 1st becomes 2nd
        list->head->prev = list_node;       //previous 1st->prev=new 1st(list_node)
        list->head = list_node;             //head points to list_node
        // pointer drawing breakpoint
    } else
    {
        // pointer drawing breakpoint
        if (index <dpl_size(list))
        { // covers case 4
            ref_at_index = dpl_get_reference_at_index(list, index);
            assert(ref_at_index != NULL);

            list_node->prev=ref_at_index->prev;
            list_node->next=ref_at_index;
            ref_at_index->prev->next=list_node;
            ref_at_index->prev=list_node;

            // pointer drawing breakpoint
        } else
        { // covers case 3
            ref_at_index=dpl_get_last_reference(list);
            list_node->prev=ref_at_index;
            ref_at_index->next=list_node;
            list_node->next=NULL;
            // pointer drawing breakpoint
        }
    }

    if(insert_copy==true)
        list_node->element=list->element_copy(element);
    else
        list_node->element=element;

    return list;
}

dplist_t * dpl_remove_at_index( dplist_t * list, int index, bool free_element)
{
    dplist_node_t * list_node;
    //check whether there's invalid error
    DPLIST_ERR_HANDLER(list==NULL,DPLIST_INVALID_ERROR);
    //check whether there's memory error
    // pointer drawing breakpoint
    if (list->head == NULL)
    { // covers case 1
        return list;
    }
    else if (index <= 0)
    { // covers case 2
        list_node = dpl_get_first_reference(list);
        //make the input node as the first node of the list
        if(dpl_size(list)==1)
        {
            list->head=NULL;
        } else
        {
            list_node->next->prev=NULL;
            list->head=list_node->next;
            list_node->prev=NULL;
            list_node->next=NULL;
        }
    }
    else
    {
        // pointer drawing breakpoint
        if (index < dpl_size(list)-1)
        { // covers case 4
            list_node = dpl_get_reference_at_index(list, index);

            list_node->next->prev = list_node->prev;
            list_node->prev->next = list_node->next;
            list_node->prev=NULL;
            list_node->next=NULL;
            // pointer drawing breakpoint
        } else
        { // covers case 3

            if(dpl_size(list)==1)
            {
                list_node=list->head;//the only node==the first node
                list->head=NULL;
            } else
            {
                list_node=dpl_get_last_reference(list);

                list_node->prev->next = NULL;
                list_node->prev = NULL;
            }
            // pointer drawing breakpoint
        }
    }

    //printf("freeing element\n");
    if(free_element==true)
    {
        //printf("doing element_free\n");
        list->element_free(&(list_node->element));
    }
    //printf("free(list_node)\n");
    free(list_node);

    //printf("remove successfully\n");
    return list;
}

int dpl_size( dplist_t * list)
{
    if(list==NULL || list->head==NULL)
        return 0;

    int size=0;
    dplist_node_t *list_node=list->head;

    while(list_node!=NULL)
    {
        size++;
        list_node=list_node->next;
    }
    return size;
}

dplist_node_t * dpl_get_reference_at_index( dplist_t * list, int index )
{
    if(list==NULL || list->head==NULL)
        return NULL;

    int length=dpl_size(list);
    if(index<=0)
        return list->head;
    else if(index+1>=length)
        return dpl_get_last_reference(list);
    else
    {
        dplist_node_t *list_node=list->head;
        for(int i=0; i<index; i++)
        {
            list_node=list_node->next;
        }
        return list_node;
    }
}

void * dpl_get_element_at_index( dplist_t * list, int index )
{
    if(list==NULL || list->head==NULL)
        return (void *)0;

    dplist_node_t *list_node=dpl_get_reference_at_index(list, index);
    return list_node->element;
}

int dpl_get_index_of_element( dplist_t * list, void * element )
{
    if(list==NULL || list->head==NULL)
        return -1;

    dplist_node_t *list_node=list->head;
    int index=0;

    while(list_node!=NULL)
    {
        if(list->element_compare(list_node->element, element)==0)
            return index;
        list_node=list_node->next;
        index++;
    }

    printf("element not found\n");
    return -1;
}

// HERE STARTS THE EXTRA SET OF OPERATORS //

// ---- list navigation operators ----//

dplist_node_t * dpl_get_first_reference( dplist_t * list )
{
    if(list==NULL || list->head==NULL)
        return NULL;

    return list->head;
}

dplist_node_t * dpl_get_last_reference( dplist_t * list )
{
    if(list==NULL || list->head==NULL)
        return NULL;

    dplist_node_t *list_node=list->head;

    while((list_node->next)!=NULL)
    {
        list_node=list_node->next;
    }

    return list_node;
}

dplist_node_t * dpl_get_next_reference( dplist_t * list, dplist_node_t * reference )
{
    if(list==NULL || list->head==NULL || reference==NULL)
        return NULL;

    int index=dpl_get_index_of_reference(list, reference);
    if(index==-1)
    {
        printf("reference not found");
        return NULL;
    }
    else
        return reference->next;
}

dplist_node_t * dpl_get_previous_reference( dplist_t * list, dplist_node_t * reference )
{
    if(list==NULL || list->head==NULL || reference==NULL)
        return NULL;

    int index=dpl_get_index_of_reference(list, reference);
    if(index==-1)
    {
        printf("reference not found");
        return NULL;
    }
    return reference->prev;
}

// ---- search & find operators ----//

void * dpl_get_element_at_reference( dplist_t * list, dplist_node_t * reference )
{
    
    if(list==NULL || list->head==NULL)
        return NULL;

    if(reference==NULL)
        return dpl_get_last_reference(list)->element;

    int index=dpl_get_index_of_reference(list, reference);
    if(index==-1)
        return NULL;

    return reference->element;
}

dplist_node_t * dpl_get_reference_of_element( dplist_t * list, void * element )
{
    if(list==NULL || list->head==NULL)
        return NULL;

    dplist_node_t *list_node=list->head;
    int index=0;

    while(list_node!=NULL)
    {
        if(list->element_compare(list_node->element,element)==0)
            return list_node;

        list_node=list_node->next;
        index++;
    }

    printf("element not found");
    return NULL;
}

int dpl_get_index_of_reference( dplist_t * list, dplist_node_t * reference )
{
    if(list==NULL || list->head==NULL)
        return -1;

    if(reference==NULL)
        return dpl_size(list)-1;
    
    dplist_node_t *list_node=list->head;
    int index=0;
    while(list_node!=NULL)
    {
        if(list_node==reference)
            return index;
        list_node=list_node->next;
        index++;
    }
    printf("reference not found");
    return -1;
}

// ---- extra insert & remove operators ----//

dplist_t * dpl_insert_at_reference( dplist_t * list, void * element, dplist_node_t * reference, bool insert_copy )
{
    if(reference==NULL)
    {
        dpl_insert_at_index(list, element, dpl_size(list), insert_copy);
        return list;
    }
    else if(dpl_get_index_of_reference(list, reference)==-1)
        return list;
    else
    {
        int index=dpl_get_index_of_reference(list, reference);
        dpl_insert_at_index(list, element, index, insert_copy);
        return list;
    }

}

dplist_t * dpl_insert_sorted( dplist_t * list, void * element, bool insert_copy )
{
    DPLIST_ERR_HANDLER(list==NULL,DPLIST_INVALID_ERROR); //if list==NULL

    int index=0;
    dplist_node_t *list_node=list->head;

    while(list_node!=NULL)
    {
        if(list->element_compare(list_node->element,element)==1)
        {
            return dpl_insert_at_index(list,element,index,insert_copy);
        }
        list_node=list_node->next;
        index++;
    }

    return dpl_insert_at_index(list,element,index,insert_copy); //if element>any element, insert at the end of the list

}

dplist_t * dpl_remove_at_reference( dplist_t * list, dplist_node_t * reference, bool free_element )
{
    if(list==NULL || list->head==NULL)
        return list;
    if(dpl_get_index_of_reference(list, reference)==-1)
        return list;
    if(reference==NULL)
    {
        dpl_remove_at_index(list, dpl_size(list)-1, free_element);
        return list;
    } else
    {
        int index=dpl_get_index_of_reference(list, reference);
        dpl_remove_at_index(list, index, free_element);
        return list;
    }
}

dplist_t * dpl_remove_element( dplist_t * list, void * element, bool free_element )
{
    int index=dpl_get_index_of_element(list, element);
    if(index==-1)
        return list;
    else
    {
        dpl_remove_at_index(list, index, free_element);
        return list;
    }
}

// ---- you can add your extra operators here ----//



