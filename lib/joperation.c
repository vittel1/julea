/*
 * Copyright (c) 2010-2013 Michael Kuhn
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/**
 * \file
 **/

#include <julea-config.h>

#include <glib.h>

#include <joperation-internal.h>

/**
 * \defgroup JOperation Operation
 *
 * @{
 **/

/**
 * Creates a new operation.
 *
 * \author Michael Kuhn
 *
 * \code
 * \endcode
 *
 * \return A new operation. Should be freed with j_operation_free().
 **/
JOperation*
j_operation_new (JOperationType type)
{
	JOperation* operation;

	operation = g_slice_new(JOperation);
	operation->type = type;
	operation->key = NULL;

	return operation;
}

/**
 * Frees the memory allocated by an operation.
 *
 * \private
 *
 * \author Michael Kuhn
 *
 * \code
 * \endcode
 *
 * \param data An operation.
 **/
/* FIXME */
void
j_operation_free (JOperation* operation)
{
	switch (operation->type)
	{
		case J_OPERATION_CREATE_STORE:
			j_store_unref(operation->u.create_store.store);
			break;
		case J_OPERATION_DELETE_STORE:
			j_store_unref(operation->u.delete_store.store);
			break;
		case J_OPERATION_GET_STORE:
			g_free(operation->u.get_store.name);
			break;
		case J_OPERATION_STORE_CREATE_COLLECTION:
			j_store_unref(operation->u.store_create_collection.store);
			j_collection_unref(operation->u.store_create_collection.collection);
			break;
		case J_OPERATION_STORE_DELETE_COLLECTION:
			j_store_unref(operation->u.store_delete_collection.store);
			j_collection_unref(operation->u.store_delete_collection.collection);
			break;
		case J_OPERATION_STORE_GET_COLLECTION:
			j_store_unref(operation->u.store_get_collection.store);
			g_free(operation->u.store_get_collection.name);
			break;
		case J_OPERATION_COLLECTION_CREATE_ITEM:
			j_collection_unref(operation->u.collection_create_item.collection);
			j_item_unref(operation->u.collection_create_item.item);
			break;
		case J_OPERATION_COLLECTION_DELETE_ITEM:
			j_collection_unref(operation->u.collection_delete_item.collection);
			j_item_unref(operation->u.collection_delete_item.item);
			break;
		case J_OPERATION_COLLECTION_GET_ITEM:
			j_collection_unref(operation->u.collection_get_item.collection);
			g_free(operation->u.collection_get_item.name);
			break;
		case J_OPERATION_ITEM_GET_STATUS:
			j_item_unref(operation->u.item_get_status.item);
			break;
		case J_OPERATION_ITEM_READ:
			j_item_unref(operation->u.item_read.item);
			break;
		case J_OPERATION_ITEM_WRITE:
			j_item_unref(operation->u.item_write.item);
			break;
		case J_OPERATION_NONE:
		default:
			g_warn_if_reached();
	}

	g_slice_free(JOperation, operation);
}

/**
 * @}
 **/
