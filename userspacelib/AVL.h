#ifndef __AVL_H__
#define __AVL_H__

struct ptr_node
{
	void *data;
	int height;
	struct ptr_node *left;
	struct ptr_node *right;
};

struct ptr_node *Delete(struct ptr_node *root,void *data);

struct ptr_node *Insert(struct ptr_node* root,void *data);

#ifndef __AVL_IMPL__
#define __AVL_IMPL__
#include <stdlib.h>
//Insertion and deletion in AVL Tree
struct ptr_node *Newptr_node(void *data)
{
	struct ptr_node *temp = (struct ptr_node *)malloc(sizeof(struct ptr_node));
	temp->data = data;
	temp->left = NULL;
	temp->right = NULL;
	temp->height = 1;
	return temp;
}

static inline int max(int a,int b)
{
 	return (a>b)?a:b;
}

static inline int height(struct ptr_node *ptr_node)
{
	if(ptr_node==NULL)
		return 0;

    return ptr_node->height;
}

static inline int Balance(struct ptr_node *ptr_node)
{
	if(ptr_node==NULL)
		return 0;
	return height(ptr_node->left) - height(ptr_node->right);
}

static inline struct ptr_node *LeftRotate(struct ptr_node *z)
{
	struct ptr_node *y = z->right;
	struct ptr_node *t2 = y->left;
	y->left = z;
	z->right = t2;
	z->height = max(height(z->left),height(z->right))+1;
	y->height = max(height(y->left),height(y->right))+1;

	return y;
}

static inline struct ptr_node *RightRotate(struct ptr_node *z)
{
	struct ptr_node *y = z->left;
	struct ptr_node *t3 = y->right;
	y->right = z;
	z->left = t3;
	z->height = max(height(z->left),height(z->right))+1;
	y->height = max(height(y->left),height(y->right))+1;

	return y;
}

static inline struct ptr_node *FindMin(struct ptr_node *ptr_node)
{
	while(ptr_node->left!=NULL)
		ptr_node = ptr_node->left;
	return ptr_node;
}

struct ptr_node *Delete(struct ptr_node *root,void *data)
{
	if(root==NULL)
		return root;
	if(data < root->data)
		root->left = Delete(root->left,data);
	else if(data > root->data)
		root->right = Delete(root->right,data);
	else
	{
		if(root->right==NULL && root->left==NULL)
		{
			free(root);
			root = NULL;
		}
		else if(root->left!=NULL && root->right==NULL)
		{
			struct ptr_node *temp = root->left;
			root = root->left;
			free(temp);
		}
		else if(root->right!=NULL && root->left==NULL)
		{
			struct ptr_node *temp = root->right;
			root = root->right;
			free(temp);
		}
		else
		{
			struct ptr_node *temp = FindMin(root->right);
			root->data = temp->data;
			root->right = Delete(root->right,temp->data);
		}
	}
	if(root==NULL)
		return root;
	root->height = 1 + max(height(root->left),height(root->right));
	int balance = Balance(root);
	//Left Left Case
	if(balance > 1 && Balance(root->left) >=0)
		return RightRotate(root);
	// Right Right Case
	if(balance < -1 && Balance(root->right) <=0)
		return LeftRotate(root);
	// Left Right Case
	if(balance > 1 && Balance(root->left) < 0)
	{
		root->left = LeftRotate(root->left);
		return RightRotate(root);
	}
	//Right Left Case
	if(balance < -1 && Balance(root->right) > 0)
	{
		root->right = RightRotate(root->right);
		return LeftRotate(root);
	}
	return root;
}

struct ptr_node *Insert(struct ptr_node *root,void *data)
{
	if(root==NULL)
		return Newptr_node(data);
	if(data < root->data)
		root->left = Insert(root->left,data);
	else if(data > root->data)
		root->right = Insert(root->right,data);
	else
		return root;
	root->height = max(height(root->left),height(root->right))+1;
	int balance = Balance(root);
	// Left Left Case
	if(balance > 1 && data < root->left->data)
		return RightRotate(root);
	// Right Right Case
	if(balance < -1 && data > root->right->data)
		return LeftRotate(root);
	//Left Right Case
	if(balance > 1 && data > root->left->data)
	{
		root->left = LeftRotate(root->left);
		return RightRotate(root);
	}
	// Right Left Case
	if(balance < -1 && data < root->right->data)
	{
		root->right = RightRotate(root->right);
		return LeftRotate(root);
	}
	return root;
}


#endif //__AVL_IMPL__


#endif //__AVL_H__
