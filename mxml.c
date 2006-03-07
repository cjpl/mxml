/********************************************************************\

   Name:         mxml.c
   Created by:   Stefan Ritt

   Contents:     Midas XML Library

   This is a simple implementation of XML functions for writing and
   reading XML files. For writing an XML file from scratch, following
   functions can be used:

   writer = mxml_open_file(file_name);
     mxml_start_element(writer, name);
     mxml_write_attribute(writer, name, value);
     mxml_write_value(writer, value);
     mxml_end_element(writer); 
     ...
   mxml_close_file(writer);

   To read an XML file, the function

   tree = mxml_parse_file(file_name, error, sizeof(error));

   is used. It parses the complete XML file and stores it in a
   hierarchical tree in memory. Nodes in that tree can be searched
   for with

   mxml_find_node(tree, xml_path);

   or

   mxml_find_nodes(tree, xml_path, &nodelist);

   which support a subset of the XPath specification. Another set of
   functions is availabe to retrieve attributes and values from nodes
   in the tree and for manipulating nodes, like replacing, adding and
   deleting nodes.

   $Id$

\********************************************************************/

#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <assert.h>

#ifdef _MSC_VER

#include <windows.h>
#include <io.h>
#include <time.h>

#else

#define TRUE 1
#define FALSE 0

#ifndef O_TEXT
#define O_TEXT 0
#define O_BINARY 0
#endif

#include <stdlib.h>
#include <unistd.h>
#include <ctype.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/time.h>
#include <time.h>

#endif

#include "mxml.h"
#include "strlcpy.h"

#define XML_INDENT "  "

/*------------------------------------------------------------------*/

int mxml_write_line(MXML_WRITER *writer, const char *line)
{
   int len;
   
   len = strlen(line);

   if (writer->buffer) {
      if (writer->buffer_len + len >= writer->buffer_size) {
         writer->buffer_size += 10000;
         writer->buffer = (char *)realloc(writer->buffer, writer->buffer_size);
      }
      strcat(writer->buffer, line);
      writer->buffer_len += len;
      return len;
   } else {
      return write(writer->fh, line, len);
   }

   return 0;
}


/*------------------------------------------------------------------*/

MXML_WRITER *mxml_open_buffer()
/* open a file and write XML header */
{
   char str[256], line[1000];
   time_t now;
   MXML_WRITER *writer;

   writer = (MXML_WRITER *)malloc(sizeof(MXML_WRITER));
   memset(writer, 0, sizeof(MXML_WRITER));
   writer->translate = 1;

   writer->buffer_size = 10000;
   writer->buffer = (char *)malloc(10000);
   writer->buffer[0] = 0;
   writer->buffer_len = 0;

   /* write XML header */
   strcpy(line, "<?xml version=\"1.0\" encoding=\"ISO-8859-1\"?>\n");
   mxml_write_line(writer, line);
   time(&now);
   strcpy(str, ctime(&now));
   str[24] = 0;
   sprintf(line, "<!-- created by MXML on %s -->\n", str);
   mxml_write_line(writer, line);

   /* initialize stack */
   writer->level = 0;
   writer->element_is_open = 0;

   return writer;
}

/*------------------------------------------------------------------*/

MXML_WRITER *mxml_open_file(const char *file_name) 
/* open a file and write XML header */
{
   char str[256], line[1000];
   time_t now;
   MXML_WRITER *writer;

   writer = (MXML_WRITER *)malloc(sizeof(MXML_WRITER));
   memset(writer, 0, sizeof(MXML_WRITER));
   writer->translate = 1;

   writer->fh = open(file_name, O_RDWR | O_CREAT | O_TRUNC | O_TEXT, 0644);

   if (writer->fh == -1) {
      sprintf(line, "Unable to open file \"%s\": ", file_name);
      perror(line);
      free(writer);
      return NULL;
   }

   /* write XML header */
   strcpy(line, "<?xml version=\"1.0\" encoding=\"ISO-8859-1\"?>\n");
   mxml_write_line(writer, line);
   time(&now);
   strcpy(str, ctime(&now));
   str[24] = 0;
   sprintf(line, "<!-- created by MXML on %s -->\n", str);
   mxml_write_line(writer, line);

   /* initialize stack */
   writer->level = 0;
   writer->element_is_open = 0;

   return writer;
}

/*------------------------------------------------------------------*/

void mxml_encode(char *src, int size, int translate)
/* convert '<' '>' '&' '"' ''' into &xx; */
{
   char *ps, *pd;
   static char *buffer = NULL;
   static int buffer_size = 1000;

   assert(size);

   if (buffer == NULL)
      buffer = (char *) malloc(buffer_size);

   if (size > buffer_size) {
      buffer = (char *) realloc(buffer, size*2);
      buffer_size = size;
   }

   ps = src;
   pd = buffer;
   for (ps = src ; *ps && (size_t)pd - (size_t)buffer < (size_t)(size-10) ; ps++) {

      if (translate) { // tranlate "<", ">", "&", """, "'"
         switch (*ps) {
         case '<':
            strcpy(pd, "&lt;");
            pd += 4;
            break;
         case '>':
            strcpy(pd, "&gt;");
            pd += 4;
            break;
         case '&':
            strcpy(pd, "&amp;");
            pd += 5;
            break;
         case '\"':
            strcpy(pd, "&quot;");
            pd += 6;
            break;
         case '\'':
            strcpy(pd, "&apos;");
            pd += 6;
            break;
         default:
            *pd++ = *ps;
         }
      } else {
         switch (*ps) { // translate only illegal XML characters "<" and "&"
         case '<':
            strcpy(pd, "&lt;");
            pd += 4;
            break;
         case '&':
            strcpy(pd, "&amp;");
            pd += 5;
            break;
         default:
            *pd++ = *ps;
         }
      }
   }
   *pd = 0;

   strlcpy(src, buffer, size);
}

/*------------------------------------------------------------------*/

void mxml_decode(char *str)
/* reverse of mxml_encode, strip leading or trailing '"' */
{
   char *p;

   p = str;
   while ((p = strchr(p, '&')) != NULL) {
      if (strncmp(p, "&lt;", 4) == 0) {
         *(p++) = '<';
         strcpy(p, p+3);
      }
      if (strncmp(p, "&gt;", 4) == 0) {
         *(p++) = '>';
         strcpy(p, p+3);
      }
      if (strncmp(p, "&amp;", 5) == 0) {
         *(p++) = '&';
         strcpy(p, p+4);
      }
      if (strncmp(p, "&quot;", 6) == 0) {
         *(p++) = '\"';
         strcpy(p, p+5);
      }
      if (strncmp(p, "&apos;", 6) == 0) {
         *(p++) = '\'';
         strcpy(p, p+5);
      }
   }
/*   if (str[0] == '\"' && str[strlen(str)-1] == '\"') {
      strcpy(str, str+1);
      str[strlen(str)-1] = 0;
   }*/
}

/*------------------------------------------------------------------*/

int mxml_set_translate(MXML_WRITER *writer, int flag)
/* set translation of <,>,",',&, on/off in writer */
{
   int old_flag;

   old_flag = writer->translate;
   writer->translate = flag;
   return old_flag;
}
/*------------------------------------------------------------------*/

int mxml_start_element1(MXML_WRITER *writer, const char *name, int indent)
/* start a new XML element, must be followed by mxml_end_elemnt */
{
   int i;
   char line[1000], name_enc[1000];

   if (writer->element_is_open) {
      mxml_write_line(writer, ">\n");
      writer->element_is_open = FALSE;
   }

   line[0] = 0;
   if (indent)
      for (i=0 ; i<writer->level ; i++)
         strlcat(line, XML_INDENT, sizeof(line));
   strlcat(line, "<", sizeof(line));
   strlcpy(name_enc, name, sizeof(name_enc));
   mxml_encode(name_enc, sizeof(name_enc), writer->translate);
   strlcat(line, name_enc, sizeof(line));

   /* put element on stack */
   if (writer->level == 0)
      writer->stack = (char **)malloc(sizeof(char *));
   else
      writer->stack = (char **)realloc(writer->stack, sizeof(char *)*(writer->level+1));
   
   writer->stack[writer->level] = (char *) malloc(strlen(name_enc)+1);
   strcpy(writer->stack[writer->level], name_enc);
   writer->level++;
   writer->element_is_open = TRUE;
   writer->data_was_written = FALSE;

   return mxml_write_line(writer, line) == (int)strlen(line);
}

/*------------------------------------------------------------------*/

int mxml_start_element(MXML_WRITER *writer, const char *name)
{
   return mxml_start_element1(writer, name, TRUE);
}

/*------------------------------------------------------------------*/

int mxml_start_element_noindent(MXML_WRITER *writer, const char *name)
{
   return mxml_start_element1(writer, name, FALSE);
}

/*------------------------------------------------------------------*/

int mxml_end_element(MXML_WRITER *writer)
/* close an open XML element */
{
   int i;
   char line[1000];

   if (writer->level == 0)
      return 0;
   
   writer->level--;

   if (writer->element_is_open) {
      writer->element_is_open = FALSE;
      free(writer->stack[writer->level]);
      if (writer->level == 0)
         free(writer->stack);
      strcpy(line, "/>\n");
      return mxml_write_line(writer, line) == (int)strlen(line);
   }

   line[0] = 0;
   if (!writer->data_was_written) {
      for (i=0 ; i<writer->level ; i++)
         strlcat(line, XML_INDENT, sizeof(line));
   }

   strlcat(line, "</", sizeof(line));
   strlcat(line, writer->stack[writer->level], sizeof(line));
   free(writer->stack[writer->level]);
   if (writer->level == 0)
      free(writer->stack);
   strlcat(line, ">\n", sizeof(line));
   writer->data_was_written = FALSE;

   return mxml_write_line(writer, line) == (int)strlen(line);
}

/*------------------------------------------------------------------*/

int mxml_write_attribute(MXML_WRITER *writer, const char *name, const char *value)
/* write an attribute to the currently open XML element */
{
   char name_enc[1000], val_enc[1000], line[2000];

   if (!writer->element_is_open)
      return FALSE;

   strcpy(name_enc, name);
   mxml_encode(name_enc, sizeof(name_enc), writer->translate);
   strcpy(val_enc, value);
   mxml_encode(val_enc, sizeof(val_enc), writer->translate);

   sprintf(line, " %s=\"%s\"", name_enc, val_enc);

   return mxml_write_line(writer, line) == (int)strlen(line);
}

/*------------------------------------------------------------------*/

int mxml_write_value(MXML_WRITER *writer, const char *data)
/* write value of an XML element, like <[name]>[value]</[name]> */
{
   static char *data_enc;
   static int data_size = 0;

   if (!writer->element_is_open)
      return FALSE;

   if (mxml_write_line(writer, ">") != 1)
      return FALSE;
   writer->element_is_open = FALSE;
   writer->data_was_written = TRUE;

   if (data_size == 0) {
      data_enc = (char *)malloc(1000);
      data_size = 1000;
   } else if ((int)strlen(data)*2+1000 > data_size) {
      data_size = 1000+strlen(data)*2;
      data_enc = (char *)realloc(data_enc, data_size);
   }

   strcpy(data_enc, data);
   mxml_encode(data_enc, data_size, writer->translate);
   return mxml_write_line(writer, data_enc) == (int)strlen(data_enc);
}

/*------------------------------------------------------------------*/

int mxml_write_comment(MXML_WRITER *writer, const char *string)
/* write a comment to an XML file, enclosed in "<!--" and "-->" */
{
   int  i;
   char line[1000];

   if (writer->element_is_open) {
      mxml_write_line(writer, ">\n");
      writer->element_is_open = FALSE;
   }

   line[0] = 0;
   for (i=0 ; i<writer->level ; i++)
      strlcat(line, XML_INDENT, sizeof(line));

   strlcat(line, "<!-- ", sizeof(line));
   strlcat(line, string, sizeof(line));
   strlcat(line, " -->\n", sizeof(line));
   if (mxml_write_line(writer, line) != (int)strlen(line))
      return FALSE;

   return TRUE;
}

/*------------------------------------------------------------------*/

char *mxml_close_buffer(MXML_WRITER *writer)
/* close a file opened with mxml_open_writer */
{
   int i;
   char *p;

   if (writer->element_is_open) {
      writer->element_is_open = FALSE;
      if (mxml_write_line(writer, ">\n") != 2)
         return NULL;
   }

   /* close remaining open levels */
   for (i = 0 ; i<writer->level ; i++)
      mxml_end_element(writer);

   p = writer->buffer;
   free(writer);
   return p;
}

/*------------------------------------------------------------------*/

int mxml_close_file(MXML_WRITER *writer)
/* close a file opened with mxml_open_writer */
{
   int i;

   if (writer->element_is_open) {
      writer->element_is_open = FALSE;
      if (mxml_write_line(writer, ">\n") != 2)
         return 0;
   }

   /* close remaining open levels */
   for (i = 0 ; i<writer->level ; i++)
      mxml_end_element(writer);

   close(writer->fh);
   free(writer);
   return 1;
}

/*------------------------------------------------------------------*/

PMXML_NODE mxml_create_root_node()
/* create root node of an XML tree */
{
   PMXML_NODE root;

   root = (PMXML_NODE)calloc(sizeof(MXML_NODE), 1);
   strcpy(root->name, "root");
   root->node_type = DOCUMENT_NODE;

   return root;
}

/*------------------------------------------------------------------*/

PMXML_NODE mxml_add_special_node_at(PMXML_NODE parent, int node_type, char *node_name, char *value, int index)
/* add a subnode (child) to an existing parent node as a specific position */
{
   PMXML_NODE pnode, pchild;
   int i, j;

   assert(parent);
   if (parent->n_children == 0)
      parent->child = (PMXML_NODE)malloc(sizeof(MXML_NODE));
   else {
      pchild = parent->child;
      parent->child = (PMXML_NODE)realloc(parent->child, sizeof(MXML_NODE)*(parent->n_children+1));

      if (parent->child != pchild) {
         /* correct parent pointer for children */
         for (i=0 ; i<parent->n_children ; i++) {
            pchild = parent->child+i;
            for (j=0 ; j<pchild->n_children ; j++)
               pchild->child[j].parent = pchild;
         }
      }
   }
   assert(parent->child);

   /* move following nodes one down */
   if (index < parent->n_children) 
      for (i=parent->n_children ; i > index ; i--)
         memcpy(&parent->child[i], &parent->child[i-1], sizeof(MXML_NODE));

   /* initialize new node */
   pnode = &parent->child[index];
   memset(pnode, 0, sizeof(MXML_NODE));
   strlcpy(pnode->name, node_name, sizeof(pnode->name));
   pnode->node_type = node_type;
   pnode->parent = parent;
   
   parent->n_children++;

   if (value && *value) {
      pnode->value = (char *)malloc(strlen(value)+1);
      assert(pnode->value);
      strcpy(pnode->value, value);
   }

   return pnode;
}

/*------------------------------------------------------------------*/

PMXML_NODE mxml_add_special_node(PMXML_NODE parent, int node_type, char *node_name, char *value)
/* add a subnode (child) to an existing parent node at the end */
{
   return mxml_add_special_node_at(parent, node_type, node_name, value, parent->n_children);
}

/*------------------------------------------------------------------*/

PMXML_NODE mxml_add_node(PMXML_NODE parent, char *node_name, char *value)
/* add a subnode (child) to an existing parent node at the end */
{
   return mxml_add_special_node_at(parent, ELEMENT_NODE, node_name, value, parent->n_children);
}

/*------------------------------------------------------------------*/

PMXML_NODE mxml_add_node_at(PMXML_NODE parent, char *node_name, char *value, int index)
/* add a subnode (child) to an existing parent node at the end */
{
   return mxml_add_special_node_at(parent, ELEMENT_NODE, node_name, value, index);
}

/*------------------------------------------------------------------*/

int mxml_add_tree_at(PMXML_NODE parent, PMXML_NODE tree, int index)
/* add a whole node tree to an existing parent node at a specific position */
{
   PMXML_NODE pchild;
   int i, j;

   assert(parent);
   assert(tree);
   if (parent->n_children == 0)
      parent->child = (PMXML_NODE)malloc(sizeof(MXML_NODE));
   else {
      pchild = parent->child;
      parent->child = (PMXML_NODE)realloc(parent->child, sizeof(MXML_NODE)*(parent->n_children+1));

      if (parent->child != pchild) {
         /* correct parent pointer for children */
         for (i=0 ; i<parent->n_children ; i++) {
            pchild = parent->child+i;
            for (j=0 ; j<pchild->n_children ; j++)
               pchild->child[j].parent = pchild;
         }
      }
   }
   assert(parent->child);

   if (index < parent->n_children) 
      for (i=parent->n_children ; i > index ; i--) {
         /* move following nodes one down */
         memcpy(&parent->child[i], &parent->child[i-1], sizeof(MXML_NODE));

         /* correct parent pointer for children */
         for (i=0 ; i<parent->n_children ; i++) {
            pchild = parent->child+i;
            for (j=0 ; j<pchild->n_children ; j++)
               pchild->child[j].parent = pchild;
         }
      }

   /* initialize new node */
   memcpy(parent->child+index, tree, sizeof(MXML_NODE));
   parent->n_children++;
   parent->child[index].parent = parent;

   /* correct parent pointer for children */
   for (i=0 ; i<parent->n_children ; i++) {
      pchild = parent->child+i;
      for (j=0 ; j<pchild->n_children ; j++)
         pchild->child[j].parent = pchild;
   }

   return TRUE;
}

/*------------------------------------------------------------------*/

int mxml_add_tree(PMXML_NODE parent, PMXML_NODE tree)
/* add a whole node tree to an existing parent node at the end */
{
   return mxml_add_tree_at(parent, tree, parent->n_children);
}

/*------------------------------------------------------------------*/

int mxml_add_attribute(PMXML_NODE pnode, char *attrib_name, char *attrib_value)
/* add an attribute to an existing node */
{
   if (pnode->n_attributes == 0) {
      pnode->attribute_name  = (char*)malloc(MXML_NAME_LENGTH);
      pnode->attribute_value = (char**)malloc(sizeof(char *));
   } else {
      pnode->attribute_name  = (char*)realloc(pnode->attribute_name,  MXML_NAME_LENGTH*(pnode->n_attributes+1));
      pnode->attribute_value = (char**)realloc(pnode->attribute_value, sizeof(char *)*(pnode->n_attributes+1));
   }

   strlcpy(pnode->attribute_name+pnode->n_attributes*MXML_NAME_LENGTH, attrib_name, MXML_NAME_LENGTH);
   pnode->attribute_value[pnode->n_attributes] = (char *)malloc(strlen(attrib_value)+1);
   strcpy(pnode->attribute_value[pnode->n_attributes], attrib_value);
   pnode->n_attributes++;

   return TRUE;
}

/*------------------------------------------------------------------*/

int mxml_get_number_of_children(PMXML_NODE pnode)
/* return number of subnodes (children) of a node */
{
   assert(pnode);
   return pnode->n_children;
}

/*------------------------------------------------------------------*/

PMXML_NODE mxml_subnode(PMXML_NODE pnode, int index)
/* return number of subnodes (children) of a node */
{
   assert(pnode);
   if (index < pnode->n_children)
      return &pnode->child[index];
   return NULL;
}

/*------------------------------------------------------------------*/

int mxml_find_nodes1(PMXML_NODE tree, char *xml_path, PMXML_NODE **nodelist, int *found);

int mxml_add_resultnode(PMXML_NODE node, char *xml_path, PMXML_NODE **nodelist, int *found)
{
   /* if at end of path, add this node */
   if (*xml_path == 0) {
      if (*found == 0)
         *nodelist = (PMXML_NODE *)malloc(sizeof(PMXML_NODE));
      else
         *nodelist = (PMXML_NODE *)realloc(*nodelist, sizeof(PMXML_NODE)*(*found + 1));

      (*nodelist)[*found] = node;
      (*found)++;
   } else {
      /* if not at end of path, branch into subtree */
      return mxml_find_nodes1(node, xml_path+1, nodelist, found);
   }

   return 1;
}

/*------------------------------------------------------------------*/

int mxml_find_nodes1(PMXML_NODE tree, char *xml_path, PMXML_NODE **nodelist, int *found)
/*
   Return list of XML nodes with a subset of XPATH specifications.
   Following elemets are possible

   /<node>/<node>/..../<node>          Find a node in the tree hierarchy
   /<node>[index]                      Find child #[index] of node (index starts from 1)
   /<node>[index]/<node>               Find subnode of the above
   /<node>[<subnode>=<value>]          Find a node which has a specific subnode
   /<node>[<subnode>=<value>]/<node>   Find subnode of the above
   /<node>[@<attrib>=<value>]/<node>   Find a node which has a specific attribute
*/
{
   PMXML_NODE pnode;
   char *p1, *p2, *p3, node_name[256], condition[256];
   char cond_name[MXML_MAX_CONDITION][256], cond_value[MXML_MAX_CONDITION][256];
   int  cond_type[MXML_MAX_CONDITION];
   int i, j, k, index, num_cond;
   int cond_satisfied,cond_index;
   size_t len;

   p1 = xml_path;
   pnode = tree;

   /* skip leading '/' */
   if (*p1 && *p1 == '/')
      p1++;

   do {
      p2 = p1;
      while (*p2 && *p2 != '/' && *p2 != '[')
         p2++;
      len = (size_t)p2 - (size_t)p1;
      if (len >= sizeof(node_name))
         return 0;

      memcpy(node_name, p1, len);
      node_name[len] = 0;
      index = 0;
      num_cond = 0;
      while (*p2 == '[') {
         cond_name[num_cond][0] = cond_value[num_cond][0] = cond_type[num_cond] = 0;
         p2++;
         if (isdigit(*p2)) {
            /* evaluate [index] */
            index = atoi(p2);
            p2 = strchr(p2, ']');
            if (p2 == NULL)
               return 0;
            p2++;
         } else {
            /* evaluate [<@attrib>/<subnode>=<value>] */
            while (*p2 && isspace(*p2))
               p2++;
            strlcpy(condition, p2, sizeof(condition));
            if (strchr(condition, ']'))
               *strchr(condition, ']') = 0;
            else
               return 0;
            p2 = strchr(p2, ']')+1;
            if ((p3 = strchr(condition, '=')) != NULL) {

               if (condition[0] == '@')
                  cond_type[num_cond] = 1;

               strlcpy(cond_name[num_cond], condition, sizeof(cond_name[num_cond]));
               *strchr(cond_name[num_cond], '=') = 0;
               while (cond_name[num_cond][0] && isspace(cond_name[num_cond][strlen(cond_name[num_cond])-1]))
                  cond_name[num_cond][strlen(cond_name[num_cond])-1] = 0;

               p3++;
               while (*p3 && isspace(*p3))
                  p3++;
               if (*p3 == '\"') {
                  strlcpy(cond_value[num_cond], p3+1, sizeof(cond_value[num_cond]));
                  while (cond_value[num_cond][0] && isspace(cond_value[num_cond][strlen(cond_value[num_cond])-1]))
                     cond_value[num_cond][strlen(cond_value[num_cond])-1] = 0;
                  if (cond_value[num_cond][0] && cond_value[num_cond][strlen(cond_value[num_cond])-1] == '\"')
                     cond_value[num_cond][strlen(cond_value[num_cond])-1] = 0;
               } else if (*p3 == '\'') {
                  strlcpy(cond_value[num_cond], p3+1, sizeof(cond_value[num_cond]));
                  while (cond_value[num_cond][0] && isspace(cond_value[num_cond][strlen(cond_value[num_cond])-1]))
                     cond_value[num_cond][strlen(cond_value[num_cond])-1] = 0;
                  if (cond_value[num_cond][0] && cond_value[num_cond][strlen(cond_value[num_cond])-1] == '\'')
                     cond_value[num_cond][strlen(cond_value[num_cond])-1] = 0;
               } else {
                  strlcpy(cond_value[num_cond], p3, sizeof(cond_value[num_cond]));
                  while (cond_value[num_cond][0] && isspace(cond_value[num_cond][strlen(cond_value[num_cond])-1]))
                     cond_value[num_cond][strlen(cond_value[num_cond])-1] = 0;
               }
               num_cond++;
            }
         }
      }

      cond_index = 0;
      for (i=j=0 ; i<pnode->n_children ; i++) {
         if (num_cond) {
            cond_satisfied = 0;
            for (k=0;k<num_cond;k++) {
               if (cond_type[k]) {
                  /* search node with attribute */
                  if (strcmp(pnode->child[i].name, node_name) == 0)
                     if (mxml_get_attribute(pnode->child+i, cond_name[k]) &&
                        strcmp(mxml_get_attribute(pnode->child+i, cond_name[k]), cond_value[k]) == 0)
                        cond_satisfied++;
               }
               else {
                  /* search subnode */
                  for (j=0 ; j<pnode->child[i].n_children ; j++)
                     if (strcmp(pnode->child[i].child[j].name, cond_name[k]) == 0)
                        if (strcmp(pnode->child[i].child[j].value, cond_value[k]) == 0)
                           cond_satisfied++;
               }
            }
            if (cond_satisfied==num_cond) {
               cond_index++;
               if (index == 0 || cond_index == index) {
                  if (!mxml_add_resultnode(pnode->child+i, p2, nodelist, found))
                     return 0;
               }
            }
         } else {
            if (strcmp(pnode->child[i].name, node_name) == 0)
               if (index == 0 || ++j == index)
                  if (!mxml_add_resultnode(pnode->child+i, p2, nodelist, found))
                     return 0;
         }
      }

      if (i == pnode->n_children)
         return 1;

      pnode = &pnode->child[i];
      p1 = p2;
      if (*p1 == '/')
         p1++;

   } while (*p2);

   return 1;
}

/*------------------------------------------------------------------*/

int mxml_find_nodes(PMXML_NODE tree, char *xml_path, PMXML_NODE **nodelist)
{
   int status, found = 0;
   
   status = mxml_find_nodes1(tree, xml_path, nodelist, &found);

   if (status == 0)
      return -1;

   return found;
}

/*------------------------------------------------------------------*/

PMXML_NODE mxml_find_node(PMXML_NODE tree, char *xml_path)
/*
   Search for a specific XML node with a subset of XPATH specifications.
   Return first found node. For syntax see mxml_find_nodes()
*/
{
   PMXML_NODE *node, pnode;
   int n;

   n = mxml_find_nodes(tree, xml_path, &node);
   if (n > 0) {
      pnode = node[0];
      free(node);
   } else 
      pnode = NULL;

   return pnode;
}

/*------------------------------------------------------------------*/

char *mxml_get_name(PMXML_NODE pnode)
{
   assert(pnode);
   return pnode->name;
}

/*------------------------------------------------------------------*/

char *mxml_get_value(PMXML_NODE pnode)
{
   assert(pnode);
   return pnode->value;
}

/*------------------------------------------------------------------*/

char *mxml_get_attribute(PMXML_NODE pnode, char *name)
{
   int i;

   assert(pnode);
   for (i=0 ; i<pnode->n_attributes ; i++) 
      if (strcmp(pnode->attribute_name+i*MXML_NAME_LENGTH, name) == 0)
         return pnode->attribute_value[i];

   return NULL;
}

/*------------------------------------------------------------------*/

int mxml_replace_node_name(PMXML_NODE pnode, char *name)
{
   strlcpy(pnode->name, name, sizeof(pnode->name));
   return TRUE;
}

/*------------------------------------------------------------------*/

int mxml_replace_node_value(PMXML_NODE pnode, char *value)
{
   if (pnode->value)
      pnode->value = (char *)realloc(pnode->value, strlen(value)+1);
   else if (value)
      pnode->value = (char *)malloc(strlen(value)+1);
   else
      pnode->value = NULL;
   
   if (value)
      strcpy(pnode->value, value);

   return TRUE;
}

/*------------------------------------------------------------------*/

int mxml_replace_subvalue(PMXML_NODE pnode, char *name, char *value)
/* 
   replace value os a subnode, like

   <parent>
     <child>value</child>
   </parent>

   if pnode=parent, and "name"="child", then "value" gets replaced
*/
{
   int i;

   for (i=0 ; i<pnode->n_children ; i++) 
      if (strcmp(pnode->child[i].name, name) == 0)
         break;

   if (i == pnode->n_children)
      return FALSE;

   return mxml_replace_node_value(&pnode->child[i], value);
}

/*------------------------------------------------------------------*/

int mxml_replace_attribute_name(PMXML_NODE pnode, char *old_name, char *new_name)
/* change the name of an attribute, keep its value */
{
   int i;

   for (i=0 ; i<pnode->n_attributes ; i++) 
      if (strcmp(pnode->attribute_name+i*MXML_NAME_LENGTH, old_name) == 0)
         break;

   if (i == pnode->n_attributes)
      return FALSE;

   strlcpy(pnode->attribute_name+i*MXML_NAME_LENGTH, new_name, MXML_NAME_LENGTH);
   return TRUE;
}

/*------------------------------------------------------------------*/

int mxml_replace_attribute_value(PMXML_NODE pnode, char *attrib_name, char *attrib_value)
/* change the value of an attribute */
{
   int i;

   for (i=0 ; i<pnode->n_attributes ; i++) 
      if (strcmp(pnode->attribute_name+i*MXML_NAME_LENGTH, attrib_name) == 0)
         break;

   if (i == pnode->n_attributes)
      return FALSE;

   pnode->attribute_value[i] = (char *)realloc(pnode->attribute_value[i], strlen(attrib_value)+1);
   strcpy(pnode->attribute_value[i], attrib_value);
   return TRUE;
}

/*------------------------------------------------------------------*/

int mxml_delete_node(PMXML_NODE pnode)
/* free memory of a node and remove it from the parent's child list */
{
   PMXML_NODE parent;
   int i, j;

   /* remove node from parent's list */
   parent = pnode->parent;

   if (parent) {
      for (i=0 ; i<parent->n_children ; i++)
         if (&parent->child[i] == pnode)
            break;

      /* free allocated node memory recursively */
      mxml_free_tree(pnode);

      if (i < parent->n_children) {
         for (j=i ; j<parent->n_children-1 ; j++)
            memcpy(&parent->child[j], &parent->child[j+1], sizeof(MXML_NODE));
         parent->n_children--;
         if (parent->n_children)
            parent->child = (PMXML_NODE)realloc(parent->child, sizeof(MXML_NODE)*(parent->n_children));
         else
            free(parent->child);
      }
   } else 
      mxml_free_tree(pnode);

   return TRUE;
}

/*------------------------------------------------------------------*/

int mxml_delete_attribute(PMXML_NODE pnode, char *attrib_name)
{
   int i, j;

   for (i=0 ; i<pnode->n_attributes ; i++) 
      if (strcmp(pnode->attribute_name+i*MXML_NAME_LENGTH, attrib_name) == 0)
         break;

   if (i == pnode->n_attributes)
      return FALSE;

   free(pnode->attribute_value[i]);
   for (j=i ; j<pnode->n_attributes-1 ; j++) {
      strcpy(pnode->attribute_name+j*MXML_NAME_LENGTH, pnode->attribute_name+(j+1)*MXML_NAME_LENGTH);
      pnode->attribute_value[j] = pnode->attribute_value[j+1];
   }

   if (pnode->n_attributes > 0) {
      pnode->attribute_name  = (char *)realloc(pnode->attribute_name,  MXML_NAME_LENGTH*(pnode->n_attributes-1));
      pnode->attribute_value = (char **)realloc(pnode->attribute_value, sizeof(char *)*(pnode->n_attributes-1));
   } else {
      free(pnode->attribute_name);
      free(pnode->attribute_value);
   }

   return TRUE;
}

/*------------------------------------------------------------------*/

#define HERE root, file_name, line_number, error, error_size

PMXML_NODE read_error(PMXML_NODE root, char *file_name, int line_number, char *error, int error_size, char *format, ...)
/* used inside mxml_parse_file for reporting errors */
{
   char *msg, str[1000];
   va_list argptr;

   if (file_name && file_name[0])
      sprintf(str, "XML read error in file \"%s\", line %d: ", file_name, line_number);
   else
      sprintf(str, "XML read error, line %d: ", line_number);
   msg = (char *)malloc(error_size);
   strlcpy(error, str, error_size);

   va_start(argptr, format);
   vsprintf(str, (char *) format, argptr);
   va_end(argptr);

   strlcat(error, str, error_size);
   free(msg);
   mxml_free_tree(root);

   return NULL;
}

/*------------------------------------------------------------------*/

PMXML_NODE mxml_parse_buffer(char *buf, char *error, int error_size)
/* parse a XML buffer and convert it into a tree of MXML_NODE's. Return NULL
   in case of an error, return error description. Optional file_name is used
   for error reporting if called from mxml_parse_file() */
{
   char node_name[256], attrib_name[256], attrib_value[1000];
   char *p, *pv;
   int i,j, line_number;
   PMXML_NODE root, ptree, pnew;
   int end_element;
   size_t len;
   char *file_name = NULL; // dummy for 'HERE'

   p = buf;
   line_number = 1;

   root = mxml_create_root_node();
   ptree = root;

   /* parse file contents */
   do {
      if (*p == '<') {

         end_element = FALSE;

         /* found new element */
         p++;
         while (*p && isspace(*p)) {
            if (*p == '\n')
               line_number++;
            p++;
         }
         if (!*p)
            return read_error(HERE, "Unexpected end of file");

         if (strncmp(p, "!--", 3) == 0) {
            
            /* found comment */

            pnew = mxml_add_special_node(ptree, COMMENT_NODE, "Comment", NULL);
            pv = p+3;
            while (*pv == ' ')
               pv++;

            p += 3;
            if (strstr(p, "-->") == NULL)
               return read_error(HERE, "Unterminated comment");
            
            while (strncmp(p, "-->", 3) != 0) {
               if (*p == '\n')
                  line_number++;
               p++;
            }

            len = (size_t)p - (size_t)pv;
            pnew->value = (char *)malloc(len+1);
            memcpy(pnew->value, pv, len);
            pnew->value[len] = 0;
            mxml_decode(pnew->value);

            p += 3;

         } else if (*p == '?') {

            /* found ?...? element */
            pnew = mxml_add_special_node(ptree, PROCESSING_INSTRUCTION_NODE, "PI", NULL);
            pv = p+1;

            p++;
            if (strstr(p, "?>") == NULL)
               return read_error(HERE, "Unterminated ?...? element");
            
            while (strncmp(p, "?>", 2) != 0) {
               if (*p == '\n')
                  line_number++;
               p++;
            }

            len = (size_t)p - (size_t)pv;
            pnew->value = (char *)malloc(len+1);
            memcpy(pnew->value, pv, len);
            pnew->value[len] = 0;
            mxml_decode(pnew->value);

            p += 2;

         } else if (strncmp(p, "!DOCTYPE", 8) == 0 ) {

            /* found !DOCTYPE element , skip it */
            p += 8;
            if (strstr(p, ">") == NULL)
               return read_error(HERE, "Unterminated !DOCTYPE element");

            j = 0;
            while (*p && (*p != '>' || j > 0)) {
               if (*p == '\n')
                  line_number++;
               else if (*p == '<')
                  j++;
               else if (*p == '>')
                  j--;
               p++;
            }
            if (!*p)
               return read_error(HERE, "Unexpected end of file");

            p++;

         } else {
            
            /* found normal element */
            if (*p == '/') {
               end_element = TRUE;
               p++;
               while (*p && isspace(*p)) {
                  if (*p == '\n')
                     line_number++;
                  p++;
               }
               if (!*p)
                  return read_error(HERE, "Unexpected end of file");
            }

            /* extract node name */
            i = 0;
            node_name[i] = 0;
            while (*p && !isspace(*p) && *p != '/' && *p != '>' && *p != '<')
               node_name[i++] = *p++;
            node_name[i] = 0;
            if (!*p)
               return read_error(HERE, "Unexpected end of file");
            if (*p == '<')
               return read_error(HERE, "Unexpected \'<\' inside element \"%s\"", node_name);

            mxml_decode(node_name);

            if (end_element) {

               if (!ptree)
                  return read_error(HERE, "Found unexpected </%s>", node_name);

               /* close previously opened element */
               if (strcmp(ptree->name, node_name) != 0)
                  return read_error(HERE, "Found </%s>, expected </%s>", node_name, ptree->name);
            
               /* go up one level on the tree */
               ptree = ptree->parent;

            } else {
            
               if (ptree == NULL)
                  return read_error(HERE, "Unexpected second top level node");

               /* allocate new element structure in parent tree */
               pnew = mxml_add_node(ptree, node_name, NULL);

               while (*p && isspace(*p)) {
                  if (*p == '\n')
                     line_number++;
                  p++;
               }
               if (!*p)
                  return read_error(HERE, "Unexpected end of file");

               while (*p != '>' && *p != '/') {

                  /* found attribute */
                  pv = p;
                  while (*pv && !isspace(*pv) && *pv != '=' && *pv != '<' && *pv != '>')
                     pv++;
                  if (!*pv)
                     return read_error(HERE, "Unexpected end of file");
                  if (*pv == '<' || *pv == '>')
                     return read_error(HERE, "Unexpected \'%c\' inside element \"%s\"", *pv, node_name);

                  /* extract attribute name */
                  len = (size_t)pv - (size_t)p;
                  if (len > sizeof(attrib_name)-1)
                     len = sizeof(attrib_name)-1;
                  memcpy(attrib_name, p, len);
                  attrib_name[len] = 0;

                  p = pv;
                  while (*p && isspace(*p)) {
                     if (*p == '\n')
                        line_number++;
                     p++;
                  }
                  if (!*p)
                     return read_error(HERE, "Unexpected end of file");
                  if (*p != '=')
                     return read_error(HERE, "Expect \"=\" here");

                  p++;
                  while (*p && isspace(*p)) {
                     if (*p == '\n')
                        line_number++;
                     p++;
                  }
                  if (!*p)
                     return read_error(HERE, "Unexpected end of file");
                  if (*p != '\"')
                     return read_error(HERE, "Expect \'\"\' here");
                  p++;

                  /* extract attribute value */
                  pv = p;
                  while (*pv && *pv != '\"')
                     pv++;
                  if (!*pv)
                     return read_error(HERE, "Unexpected end of file");

                  len = (size_t)pv - (size_t)p;
                  if (len > sizeof(attrib_value)-1)
                     len = sizeof(attrib_value)-1;
                  memcpy(attrib_value, p, len);
                  attrib_value[len] = 0;

                  /* add attribute to current node */
                  mxml_add_attribute(pnew, attrib_name, attrib_value);

                  p = pv+1;
                  while (*p && isspace(*p)) {
                     if (*p == '\n')
                        line_number++;
                     p++;
                  }
                  if (!*p)
                     return read_error(HERE, "Unexpected end of file");
               }

               if (*p == '/') {

                  /* found empty node, like <node/>, just skip closing bracket */
                  p++;

                  while (*p && isspace(*p)) {
                     if (*p == '\n')
                        line_number++;
                     p++;
                  }
                  if (!*p)
                     return read_error(HERE, "Unexpected end of file");
                  if (*p != '>')
                     return read_error(HERE, "Expected \">\" after \"/\"");
                  p++;
               }

               if (*p == '>') {

                  p++;

                  /* check if we have sub-element or value */
                  pv = p;
                  while (*pv && isspace(*pv)) {
                     if (*pv == '\n')
                        line_number++;
                     pv++;
                  }
                  if (!*pv)
                     return read_error(HERE, "Unexpected end of file");

                  if (*pv == '<') {

                     /* start new subtree */
                     ptree = pnew;
                     p = pv;

                  } else {

                     /* extract value */
                     while (*pv && *pv != '<') {
                        if (*pv == '\n')
                           line_number++;
                        pv++;
                     }
                     if (!*pv)
                        return read_error(HERE, "Unexpected end of file");

                     len = (size_t)pv - (size_t)p;
                     pnew->value = (char *)malloc(len+1);
                     memcpy(pnew->value, p, len);
                     pnew->value[len] = 0;
                     mxml_decode(pnew->value);
                     p = pv;

                     ptree = pnew;
                  }
               }
            }
         }
      }

      /* go to next element */
      while (*p && *p != '<') {
         if (*p == '\n')
            line_number++;
         p++;
      }
   } while (*p);

   return root;
}

/*------------------------------------------------------------------*/

int mxml_parse_entity(char **buf, char *file_name, char *error, int error_size)
/* parse !ENTYTY entries of XML files and replace with references. Return 0
   in case of no errors, return error description. Optional file_name is used
   for error reporting if called from mxml_parse_file() */
{
   char *p;
   char *pv;
   char delimiter;
   int i, j, k, line_number;
   char *replacement;
   char entity_name[MXML_MAX_ENTITY][256];
   char entity_reference_name[MXML_MAX_ENTITY][256];
   char *entity_value[MXML_MAX_ENTITY];
   int entity_type[MXML_MAX_ENTITY];    /* internal or external */
   int nentity;
   int fh, length, len;
   char *buffer;
   PMXML_NODE root = mxml_create_root_node();   /* dummy for 'HERE' */
   int ip;                      /* counter for entity value */
   char directoryname[FILENAME_MAX];
   char filename[FILENAME_MAX];

   for (ip = 0; ip < MXML_MAX_ENTITY; ip++)
      entity_value[ip] = NULL;

   line_number = 1;
   nentity = -1;

   if (!buf || !(*buf) || !strlen(*buf))
      return 0;

   strcpy(directoryname, file_name);
   mxml_dirname(directoryname);

   /* copy string to temporary space */
   buffer = (char *) malloc(strlen(*buf) + 1);
   if (buffer == NULL) {
      read_error(HERE, "Cannot allocate memory.");
      free(buffer);
      for (ip = 0; ip < MXML_MAX_ENTITY; ip++)
         free(entity_value[ip]);
      return 1;
   }
   strcpy(buffer, *buf);

   p = strstr(buffer, "!DOCTYPE");
   if (p == NULL) {             /* no entities */
      mxml_free_tree(root);
      free(buffer);
      for (ip = 0; ip < MXML_MAX_ENTITY; ip++)
         free(entity_value[ip]);
      return 0;
   }

   pv = strstr(p, "[");
   if (pv == NULL) {            /* no entities */
      mxml_free_tree(root);
      free(buffer);
      for (ip = 0; ip < MXML_MAX_ENTITY; ip++)
         free(entity_value[ip]);
      return 0;
   }

   p = pv + 1;

   /* search !ENTITY */
   do {
      if (*p == ']')
         break;

      if (*p == '<') {

         /* found new entity */
         p++;
         while (*p && isspace(*p)) {
            if (*p == '\n')
               line_number++;
            p++;
         }
         if (!*p) {
            read_error(HERE, "Unexpected end of file");
            free(buffer);
            for (ip = 0; ip < MXML_MAX_ENTITY; ip++)
               free(entity_value[ip]);
            return 1;
         }

         if (strncmp(p, "!--", 3) == 0) {
            /* found comment */
            p += 3;
            if (strstr(p, "-->") == NULL) {
               read_error(HERE, "Unterminated comment");
               free(buffer);
               for (ip = 0; ip < MXML_MAX_ENTITY; ip++)
                  free(entity_value[ip]);
               return 1;
            }

            while (strncmp(p, "-->", 3) != 0) {
               if (*p == '\n')
                  line_number++;
               p++;
            }
            p += 3;
         }

         else if (strncmp(p, "!ENTITY", 7) == 0) {
            /* found entity */
            nentity++;
            if (nentity >= MXML_MAX_ENTITY) {
               read_error(HERE, "Too much entities");
               free(buffer);
               for (ip = 0; ip < MXML_MAX_ENTITY; ip++)
                  free(entity_value[ip]);
               return 1;
            }

            pv = p + 7;
            while (*pv == ' ')
               pv++;

            /* extract entity name */
            p = pv;

            while (*p && isspace(*p) && *p != '<' && *p != '>') {
               if (*p == '\n')
                  line_number++;
               p++;
            }
            if (!*p) {
               read_error(HERE, "Unexpected end of file");
               free(buffer);
               for (ip = 0; ip < MXML_MAX_ENTITY; ip++)
                  free(entity_value[ip]);
               return 1;
            }
            if (*p == '<' || *p == '>') {
               read_error(HERE, "Unexpected \'%c\' inside !ENTITY", *p);
               free(buffer);
               for (ip = 0; ip < MXML_MAX_ENTITY; ip++)
                  free(entity_value[ip]);
               return 1;
            }

            pv = p;
            while (*pv && !isspace(*pv) && *pv != '<' && *pv != '>')
               pv++;

            if (!*pv) {
               read_error(HERE, "Unexpected end of file");
               free(buffer);
               for (ip = 0; ip < MXML_MAX_ENTITY; ip++)
                  free(entity_value[ip]);
               return 1;
            }
            if (*pv == '<' || *pv == '>') {
               read_error(HERE, "Unexpected \'%c\' inside entity \"%s\"", *pv, &entity_name[nentity][1]);
               free(buffer);
               for (ip = 0; ip < MXML_MAX_ENTITY; ip++)
                  free(entity_value[ip]);
               return 1;
            }

            len = (size_t) pv - (size_t) p;

            entity_name[nentity][0] = '&';
            i = 1;
            entity_name[nentity][i] = 0;
            while (*p && !isspace(*p) && *p != '/' && *p != '>' && *p != '<' && i < 253)
               entity_name[nentity][i++] = *p++;
            entity_name[nentity][i++] = ';';
            entity_name[nentity][i] = 0;

            if (!*p) {
               read_error(HERE, "Unexpected end of file");
               free(buffer);
               for (ip = 0; ip < MXML_MAX_ENTITY; ip++)
                  free(entity_value[ip]);
               return 1;
            }
            if (*p == '<') {
               read_error(HERE, "Unexpected \'<\' inside entity \"%s\"", &entity_name[nentity][1]);
               free(buffer);
               for (ip = 0; ip < MXML_MAX_ENTITY; ip++)
                  free(entity_value[ip]);
               return 1;
            }

            /* extract replacement or SYSTEM */
            while (*p && isspace(*p)) {
               if (*p == '\n')
                  line_number++;
               p++;
            }
            if (!*p) {
               read_error(HERE, "Unexpected end of file");
               free(buffer);
               for (ip = 0; ip < MXML_MAX_ENTITY; ip++)
                  free(entity_value[ip]);
               return 1;
            }
            if (*p == '>') {
               read_error(HERE, "Unexpected \'>\' inside entity \"%s\"", &entity_name[nentity][1]);
               free(buffer);
               for (ip = 0; ip < MXML_MAX_ENTITY; ip++)
                  free(entity_value[ip]);
               return 1;
            }

            /* check if SYSTEM */
            if (strncmp(p, "SYSTEM", 6) == 0) {
               entity_type[nentity] = EXTERNAL_ENTITY;
               p += 6;
            } else {
               entity_type[nentity] = INTERNAL_ENTITY;
            }

            /* extract replacement */
            while (*p && isspace(*p)) {
               if (*p == '\n')
                  line_number++;
               p++;
            }
            if (!*p) {
               read_error(HERE, "Unexpected end of file");
               free(buffer);
               for (ip = 0; ip < MXML_MAX_ENTITY; ip++)
                  free(entity_value[ip]);
               return 1;
            }
            if (*p == '>') {
               read_error(HERE, "Unexpected \'>\' inside entity \"%s\"", &entity_name[nentity][1]);
               free(buffer);
               for (ip = 0; ip < MXML_MAX_ENTITY; ip++)
                  free(entity_value[ip]);
               return 1;
            }

            if (*p != '\"' && *p != '\'') {
               read_error(HERE, "Replacement was not found for entity \"%s\"", &entity_name[nentity][1]);
               free(buffer);
               for (ip = 0; ip < MXML_MAX_ENTITY; ip++)
                  free(entity_value[ip]);
               return 1;
            }
            delimiter = *p;
            p++;
            if (!*p) {
               read_error(HERE, "Unexpected end of file");
               free(buffer);
               for (ip = 0; ip < MXML_MAX_ENTITY; ip++)
                  free(entity_value[ip]);
               return 1;
            }
            pv = p;
            while (*pv && *pv != delimiter)
               pv++;

            if (!*pv) {
               read_error(HERE, "Unexpected end of file");
               free(buffer);
               for (ip = 0; ip < MXML_MAX_ENTITY; ip++)
                  free(entity_value[ip]);
               return 1;
            }
            if (*pv == '<') {
               read_error(HERE, "Unexpected \'%c\' inside entity \"%s\"", *pv, &entity_name[nentity][1]);
               free(buffer);
               for (ip = 0; ip < MXML_MAX_ENTITY; ip++)
                  free(entity_value[ip]);
               return 1;
            }

            len = (size_t) pv - (size_t) p;
            replacement = (char *) malloc(len + 1);
            if (replacement == NULL) {
               read_error(HERE, "Cannot allocate memory.");
               free(buffer);
               for (ip = 0; ip < MXML_MAX_ENTITY; ip++)
                  free(entity_value[ip]);
               return 1;
            }

            memcpy(replacement, p, len);
            replacement[len] = 0;
            mxml_decode(replacement);

            if (entity_type[nentity] == EXTERNAL_ENTITY) {
               strcpy(entity_reference_name[nentity], replacement);
            } else {
               entity_value[nentity] = (char *) malloc(strlen(replacement));
               if (entity_value[nentity] == NULL) {
                  read_error(HERE, "Cannot allocate memory.");
                  free(buffer);
                  for (ip = 0; ip < MXML_MAX_ENTITY; ip++)
                     free(entity_value[ip]);
                  return 1;
               }
               strcpy(entity_value[nentity], replacement);
            }
            free(replacement);

            p = pv;
            while (*p && isspace(*p)) {
               if (*p == '\n')
                  line_number++;
               p++;
            }
            if (!*p) {
               read_error(HERE, "Unexpected end of file");
               free(buffer);
               for (ip = 0; ip < MXML_MAX_ENTITY; ip++)
                  free(entity_value[ip]);
               return 1;
            }
         }
      }

      /* go to next element */
      while (*p && *p != '<') {
         if (*p == '\n')
            line_number++;
         p++;
      }
   } while (*p);
   nentity++;

   /* read external file */
   for (i = 0; i < nentity; i++) {
      if (entity_type[i] == EXTERNAL_ENTITY) {
         if ( entity_reference_name[i][0] == DIR_SEPARATOR ) /* absolute path */
            strcpy(filename, entity_reference_name[i]);
         else /* relative path */
            sprintf(filename, "%s%c%s", directoryname, DIR_SEPARATOR, entity_reference_name[i]);
         fh = open(filename, O_RDONLY | O_TEXT, 0644);

         if (fh == -1) {
            entity_value[i] =
                (char *) malloc(strlen(entity_reference_name[i]) + strlen("<!--  is missing -->") + 1);
            if (entity_value[i] == NULL) {
               read_error(HERE, "Cannot allocate memory.");
               free(buffer);
               for (ip = 0; ip < MXML_MAX_ENTITY; ip++)
                  free(entity_value[ip]);
               return 1;
            }
            sprintf(entity_value[i], "<!-- %s is missing -->", entity_reference_name[i]);
         } else {
            length = lseek(fh, 0, SEEK_END);
            lseek(fh, 0, SEEK_SET);
            if (length == 0) {
               entity_value[i] = (char *) malloc(1);
               if (entity_value[i] == NULL) {
                  read_error(HERE, "Cannot allocate memory.");
                  free(buffer);
                  for (ip = 0; ip < MXML_MAX_ENTITY; ip++)
                     free(entity_value[ip]);
                  return 1;
               }
               entity_value[i][0] = 0;
            } else {
               entity_value[i] = (char *) malloc(length);
               if (entity_value[i] == NULL) {
                  read_error(HERE, "Cannot allocate memory.");
                  free(buffer);
                  for (ip = 0; ip < MXML_MAX_ENTITY; ip++)
                     free(entity_value[ip]);
                  return 1;
               }

               /* read complete file at once */
               length = read(fh, entity_value[i], length);
               entity_value[i][length - 1] = 0;
               close(fh);

               /* recursive parse */
               if (mxml_parse_entity(&entity_value[i], filename, error, error_size) != 0) {
                  mxml_free_tree(root);
                  free(buffer);
                  for (ip = 0; ip < MXML_MAX_ENTITY; ip++)
                     free(entity_value[ip]);
                  return 1;
               }
            }
         }
      }
   }

   /* count length of output string */
   length = strlen(buffer);
   for (i = 0; i < nentity; i++) {
      p = buffer;
      while (1) {
         pv = strstr(p, entity_name[i]);
         if (pv) {
            length += strlen(entity_value[i]) - strlen(entity_name[i]);
            p = pv + 1;
         } else {
            break;
         }
      }
   }

   /* re-allocate memory */
   free(*buf);
   *buf = (char *) malloc(length + 1);
   if (*buf == NULL) {
      read_error(HERE, "Cannot allocate memory.");
      free(buffer);
      for (ip = 0; ip < MXML_MAX_ENTITY; ip++)
         free(entity_value[ip]);
      return 1;
   }

   /* replace entities */
   p = buffer;
   pv = *buf;
   do {
      if (*p == '&') {
         /* found entity */
         for (j = 0; j < nentity; j++) {
            if (strncmp(p, entity_name[j], strlen(entity_name[j])) == 0) {
               for (k = 0; k < (int) strlen(entity_value[j]); k++)
                  *pv++ = entity_value[j][k];
               p += strlen(entity_name[j]);
               break;
            }
         }
      }
      *pv++ = *p++;
   } while (*p);
   *pv = 0;

   free(buffer);
   for (ip = 0; ip < MXML_MAX_ENTITY; ip++)
      free(entity_value[ip]);

   mxml_free_tree(root);
   return 0;
}

/*------------------------------------------------------------------*/

PMXML_NODE mxml_parse_file(char *file_name, char *error, int error_size)
/* parse a XML file and convert it into a tree of MXML_NODE's. Return NULL
   in case of an error, return error description */
{
   char *buf, line[1000];
   int fh, length;
   PMXML_NODE root;

   if (error)
      error[0] = 0;

   fh = open(file_name, O_RDONLY | O_TEXT, 0644);

   if (fh == -1) {
      sprintf(line, "Unable to open file \"%s\": ", file_name);
      strlcat(line, strerror(errno), sizeof(line));
      strlcpy(error, line, error_size);
      return NULL;
   }

   length = lseek(fh, 0, SEEK_END);
   lseek(fh, 0, SEEK_SET);
   buf = (char *)malloc(length+1);
   if (buf == NULL) {
      close(fh);
      sprintf(line, "Cannot allocate buffer: ");
      strlcat(line, strerror(errno), sizeof(line));
      strlcpy(error, line, error_size);
      return NULL;
   }

   /* read complete file at once */
   length = read(fh, buf, length);
   buf[length] = 0;
   close(fh);

   if (mxml_parse_entity(&buf, file_name, error, error_size) != 0) {
      free(buf);
      return NULL;
   }

   root = mxml_parse_buffer(buf, error, error_size);

   free(buf);

   return root;
}

/*------------------------------------------------------------------*/

int mxml_write_subtree(MXML_WRITER *writer, PMXML_NODE tree, int indent)
/* write complete subtree recursively into file opened with mxml_open_document() */
{
   int i;

   mxml_start_element1(writer, tree->name, indent);
   for (i=0 ; i<tree->n_attributes ; i++)
      if (!mxml_write_attribute(writer, tree->attribute_name+i*MXML_NAME_LENGTH, tree->attribute_value[i]))
         return FALSE;
   
   if (tree->value)
      if (!mxml_write_value(writer, tree->value))
         return FALSE;

   for (i=0 ; i<tree->n_children ; i++)
      if (!mxml_write_subtree(writer, &tree->child[i], (tree->value == NULL) || i > 0))
         return FALSE;

   return mxml_end_element(writer);
}

/*------------------------------------------------------------------*/

int mxml_write_tree(char *file_name, PMXML_NODE tree)
/* write a complete XML tree to a file */
{
   MXML_WRITER *writer;
   int i;

   assert(tree);
   writer = mxml_open_file(file_name);
   if (!writer)
      return FALSE;

   for (i=0 ; i<tree->n_children ; i++)
      if (tree->child[i].node_type == ELEMENT_NODE) // skip PI and comments
         if (!mxml_write_subtree(writer, &tree->child[i], TRUE))
            return FALSE;

   if (!mxml_close_file(writer))
      return FALSE;

   return TRUE;
}

/*------------------------------------------------------------------*/

PMXML_NODE mxml_clone_tree(PMXML_NODE tree)
{
   PMXML_NODE clone;
   int i;

   clone = (PMXML_NODE)calloc(sizeof(MXML_NODE), 1);

   /* copy name, node_type, n_attributes and n_children */
   memcpy(clone, tree, sizeof(MXML_NODE));

   clone->value = NULL;
   mxml_replace_node_value(clone, tree->value);

   clone->attribute_name = NULL;
   clone->attribute_value = NULL;
   for (i=0 ; i<tree->n_attributes ; i++)
      mxml_add_attribute(clone, tree->attribute_name+i*MXML_NAME_LENGTH, tree->attribute_value[i]);

   clone->child = NULL;
   clone->n_children = 0;
   for (i=0 ; i<tree->n_children ; i++)
      mxml_add_tree(clone, mxml_clone_tree(mxml_subnode(tree, i)));

   return clone;
}

/*------------------------------------------------------------------*/

void mxml_debug_tree(PMXML_NODE tree, int level)
/* print XML tree for debugging */
{
   int i, j;

   for (i=0 ; i<level ; i++)
      printf("  ");
   printf("Name: %s\n", tree->name);
   for (i=0 ; i<level ; i++)
      printf("  ");
   printf("Valu: %s\n", tree->value);
   for (i=0 ; i<level ; i++)
      printf("  ");
   printf("Type: %d\n", tree->node_type);

   for (j=0 ; j<tree->n_attributes ; j++) {
      for (i=0 ; i<level ; i++)
         printf("  ");
      printf("%s: %s\n", tree->attribute_name+j*MXML_NAME_LENGTH, 
         tree->attribute_value[j]);
   }

   for (i=0 ; i<level ; i++)
      printf("  ");
   printf("Addr: %08zX\n", (size_t)tree);
   for (i=0 ; i<level ; i++)
      printf("  ");
   printf("Prnt: %08zX\n", (size_t)tree->parent);
   for (i=0 ; i<level ; i++)
      printf("  ");
   printf("NCld: %d\n", tree->n_children);

   for (i=0 ; i<tree->n_children ; i++)
      mxml_debug_tree(tree->child+i, level+1);

   if (level == 0)
      printf("\n");
}

/*------------------------------------------------------------------*/

void mxml_free_tree(PMXML_NODE tree)
/* free memory of XML tree, must be called after any 
   mxml_create_root_node() or mxml_parse_file() */
{
   int i;

   /* first free children recursively */
   for (i=0 ; i<tree->n_children ; i++)
      mxml_free_tree(&tree->child[i]);
   if (tree->n_children)
      free(tree->child);

   /* now free dynamic data */
   for (i=0 ; i<tree->n_attributes ; i++)
      free(tree->attribute_value[i]);

   if (tree->n_attributes) {
      free(tree->attribute_name);
      free(tree->attribute_value);
   }
   
   if (tree->value)
      free(tree->value);

   /* if we are the root node, free it */
   if (tree->parent == NULL)
      free(tree);
}

/*------------------------------------------------------------------*/

/*
void mxml_test()
{
   char err[256];
   PMXML_NODE tree, tree2, node;

   tree = mxml_parse_file("c:\\tmp\\test.xml", err, sizeof(err));
   tree2 = mxml_clone_tree(tree);

   printf("Orig:\n");
   mxml_debug_tree(tree, 0);

   printf("\nClone:\n");
   mxml_debug_tree(tree2, 0);

   printf("\nCombined:\n");
   node = mxml_find_node(tree2, "cddb"); 
   mxml_add_tree(tree, node);
   mxml_debug_tree(tree, 0);

   mxml_free_tree(tree);
}
*/

/*------------------------------------------------------------------*/
/* mxml_basename deletes any prefix ending with the last slash '/' character
   present in path. mxml_dirname deletes the filename portion, beginning with
   the last slash '/' character to the end of path. Followings are examples
   from these functions

    path               dirname   basename
    "/"                "/"       ""
    "."                "."       "."
    ""                 ""        ""
    "/test.txt"        "/"       "test.txt"
    "path/to/test.txt" "path/to" "test.txt"
    "test.txt          "."       "test.txt"

   Under Windows, '\\' and ':' are recognized ad separator too. */

void mxml_basename(char *path)
{
   char str[FILENAME_MAX];
   char *p;
   char *name;

   if (path) {
      strcpy(str, path);
      p = str;
      name = str;
      while (1) {
         if (*p == 0)
            break;
         if (*p == '/'
#ifdef _MSC_VER
             || *p == ':' || *p == '\\'
#endif
             )
            name = p + 1;
         p++;
      }
      strcpy(path, name);
   }

   return;
}

void mxml_dirname(char *path)
{
   char *p;
#ifdef _MSC_VER
   char *pv;
#endif

   if (!path || strlen(path) == 0)
      return;

   p = strrchr(path, '/');
#ifdef _MSC_VER
   pv = strrchr(path, ':');
   if (pv > p)
      p = pv;
   pv = strrchr(path, '\\');
   if (pv > p)
      p = pv;
#endif

   if (p == 0)                  /* current directory */
      strcpy(path, ".");
   else if (p == path)          /* root directory */
      sprintf(path, "%c", *p);
   else
      *p = 0;

   return;
}

/*------------------------------------------------------------------*/
