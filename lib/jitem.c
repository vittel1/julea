/*
 * JULEA - Flexible storage framework
 * Copyright (C) 2010-2017 Michael Kuhn
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * \file
 **/

#include <julea-config.h>

#include <glib.h>

#include <string.h>

#include <bson.h>

#include <jitem.h>
#include <jitem-internal.h>

#include <jbackground-operation-internal.h>
#include <jbatch.h>
#include <jbatch-internal.h>
#include <jcollection.h>
#include <jcollection-internal.h>
#include <jcommon-internal.h>
#include <jconnection-pool-internal.h>
#include <jcredentials-internal.h>
#include <jdistribution.h>
#include <jdistribution-internal.h>
#include <jhelper-internal.h>
#include <jlist.h>
#include <jlist-iterator.h>
#include <jlock-internal.h>
#include <jmessage-internal.h>
#include <joperation-internal.h>
#include <jsemantics.h>
#include <jtrace-internal.h>

/**
 * \defgroup JItem Item
 *
 * Data structures and functions for managing items.
 *
 * @{
 **/

/**
 * Data for background operations.
 */
struct JCollectionBackgroundData
{
	/**
	 * The connection.
	 */
	GSocketConnection* connection;

	/**
	 * The message to send.
	 */
	JMessage* message;
};

typedef struct JCollectionBackgroundData JCollectionBackgroundData;

/**
 * Data for background operations.
 */
struct JItemBackgroundData
{
	/**
	 * The connection.
	 */
	GSocketConnection* connection;

	/**
	 * The message to send.
	 */
	JMessage* message;

	guint index;

	/**
	 * The union for read and write parts.
	 */
	union
	{
		/**
		 * The read part.
		 */
		struct
		{
			/**
			 * The list of buffers to fill.
			 * Contains #JItemReadData elements.
			 */
			JList* buffer_list;
		}
		read;

		/**
		 * The read status part.
		 */
		struct
		{
			/**
			 * The list of items.
			 * Contains #JItemReadStatusData elements.
			 */
			JList* buffer_list;
		}
		read_status;

		/**
		 * The write part.
		 */
		struct
		{
			/**
			 * The create message.
			 */
			JMessage* create_message;
		}
		write;
	}
	u;
};

typedef struct JItemBackgroundData JItemBackgroundData;

/**
 * Data for buffers.
 */
struct JItemReadData
{
	/**
	 * The data.
	 */
	gpointer data;

	/**
	 * The data length.
	 */
	guint64* nbytes;
};

typedef struct JItemReadData JItemReadData;

/**
 * Data for buffers.
 */
struct JItemReadStatusData
{
	/**
	 * The item.
	 */
	JItem* item;

	/**
	 * The item status flags.
	 */
	JItemStatusFlags flags;

	guint64* sizes;
};

typedef struct JItemReadStatusData JItemReadStatusData;

/**
 * A JItem.
 **/
struct JItem
{
	/**
	 * The ID.
	 **/
	bson_oid_t id;

	/**
	 * The name.
	 **/
	gchar* name;

	JCredentials* credentials;
	JDistribution* distribution;

	/**
	 * The status.
	 **/
	struct
	{
		guint64 age;

		/**
		 * The size.
		 * Stored in bytes.
		 */
		guint64 size;

		/**
		 * The time of the last modification.
		 * Stored in microseconds since the Epoch.
		 */
		gint64 modification_time;

		gboolean* created;
	}
	status;

	/**
	 * The parent collection.
	 **/
	JCollection* collection;

	/**
	 * The reference count.
	 **/
	gint ref_count;
};

/**
 * Executes delete operations in a background operation.
 *
 * \private
 *
 * \author Michael Kuhn
 *
 * \param data Background data.
 *
 * \return #data.
 **/
static
gpointer
j_item_delete_background_operation (gpointer data)
{
	JCollectionBackgroundData* background_data = data;
	JMessage* reply;

	j_message_send(background_data->message, background_data->connection);

	if (j_message_get_type_modifier(background_data->message) & J_MESSAGE_SAFETY_NETWORK)
	{
		reply = j_message_new_reply(background_data->message);
		j_message_receive(reply, background_data->connection);

		/* FIXME do something with reply */
		j_message_unref(reply);
	}

	return data;
}

/**
 * Executes read operations in a background operation.
 *
 * \private
 *
 * \author Michael Kuhn
 *
 * \param data Background data.
 *
 * \return #data.
 **/
static
gpointer
j_item_read_background_operation (gpointer data)
{
	JItemBackgroundData* background_data = data;
	JListIterator* iterator;
	JMessage* reply;
	guint operations_done;
	guint32 operation_count;
	guint64 nbytes;

	j_message_send(background_data->message, background_data->connection);

	reply = j_message_new_reply(background_data->message);
	iterator = j_list_iterator_new(background_data->u.read.buffer_list);

	operations_done = 0;
	operation_count = j_message_get_count(background_data->message);

	while (operations_done < operation_count)
	{
		guint32 reply_operation_count;

		j_message_receive(reply, background_data->connection);

		reply_operation_count = j_message_get_count(reply);

		for (guint j = 0; j < reply_operation_count && j_list_iterator_next(iterator); j++)
		{
			JItemReadData* buffer = j_list_iterator_get(iterator);

			nbytes = j_message_get_8(reply);
			// FIXME thread-safety
			*(buffer->nbytes) += nbytes;

			if (nbytes > 0)
			{
				GInputStream* input;

				input = g_io_stream_get_input_stream(G_IO_STREAM(background_data->connection));
				g_input_stream_read_all(input, buffer->data, nbytes, NULL, NULL, NULL);
			}

			g_slice_free(JItemReadData, buffer);
		}

		operations_done += reply_operation_count;
	}

	j_list_iterator_free(iterator);
	j_message_unref(reply);

	/*
	if (nbytes < new_length)
	{
		break;
	}
	*/

	return data;
}

/**
 * Executes write operations in a background operation.
 *
 * \private
 *
 * \author Michael Kuhn
 *
 * \param data Background data.
 *
 * \return #data.
 **/
static
gpointer
j_item_write_background_operation (gpointer data)
{
	JItemBackgroundData* background_data = data;
	JMessage* reply;

	if (background_data->u.write.create_message != NULL)
	{
		j_message_send(background_data->u.write.create_message, background_data->connection);

		/* This will always be true, see j_item_write_internal(). */
		if (j_message_get_type_modifier(background_data->u.write.create_message) & J_MESSAGE_SAFETY_NETWORK)
		{
			reply = j_message_new_reply(background_data->u.write.create_message);
			j_message_receive(reply, background_data->connection);
			j_message_unref(reply);
		}
	}

	j_message_send(background_data->message, background_data->connection);

	if (j_message_get_type_modifier(background_data->message) & J_MESSAGE_SAFETY_NETWORK)
	{
		/* guint64 nbytes; */

		reply = j_message_new_reply(background_data->message);
		j_message_receive(reply, background_data->connection);

		/* FIXME do something with nbytes */
		/* nbytes = j_message_get_8(reply); */

		j_message_unref(reply);
	}

	return data;
}

/**
 * Executes get status operations in a background operation.
 *
 * \private
 *
 * \author Michael Kuhn
 *
 * \param data Background data.
 *
 * \return #data.
 **/
static
gpointer
j_item_get_status_background_operation (gpointer data)
{
	JItemBackgroundData* background_data = data;
	JListIterator* iterator;
	JMessage* reply;

	/* FIXME reply? */
	j_message_send(background_data->message, background_data->connection);

	reply = j_message_new_reply(background_data->message);
	iterator = j_list_iterator_new(background_data->u.read_status.buffer_list);

	j_message_receive(reply, background_data->connection);

	while (j_list_iterator_next(iterator))
	{
		JItemReadStatusData* buffer = j_list_iterator_get(iterator);

		if (buffer->flags & J_ITEM_STATUS_MODIFICATION_TIME)
		{
			gint64 modification_time;

			modification_time = j_message_get_8(reply);
			// FIXME thread-safety
			j_item_set_modification_time(buffer->item, modification_time);
		}

		if (buffer->flags & J_ITEM_STATUS_SIZE)
		{
			guint64 size;

			size = j_message_get_8(reply);
			// FIXME thread-safety
			//j_item_add_size(buffer->item, size);
			buffer->sizes[background_data->index] = size;
		}
	}

	j_list_iterator_free(iterator);
	j_message_unref(reply);

	return data;
}

/**
 * Increases an item's reference count.
 *
 * \author Michael Kuhn
 *
 * \code
 * JItem* i;
 *
 * j_item_ref(i);
 * \endcode
 *
 * \param item An item.
 *
 * \return #item.
 **/
JItem*
j_item_ref (JItem* item)
{
	g_return_val_if_fail(item != NULL, NULL);

	j_trace_enter(G_STRFUNC);

	g_atomic_int_inc(&(item->ref_count));

	j_trace_leave(G_STRFUNC);

	return item;
}

/**
 * Decreases an item's reference count.
 * When the reference count reaches zero, frees the memory allocated for the item.
 *
 * \author Michael Kuhn
 *
 * \code
 * \endcode
 *
 * \param item An item.
 **/
void
j_item_unref (JItem* item)
{
	g_return_if_fail(item != NULL);

	j_trace_enter(G_STRFUNC);

	if (g_atomic_int_dec_and_test(&(item->ref_count)))
	{
		if (item->collection != NULL)
		{
			j_collection_unref(item->collection);
		}

		j_credentials_unref(item->credentials);
		j_distribution_unref(item->distribution);

		g_free(item->status.created);
		g_free(item->name);

		g_slice_free(JItem, item);
	}

	j_trace_leave(G_STRFUNC);
}

/**
 * Returns an item's name.
 *
 * \author Michael Kuhn
 *
 * \code
 * \endcode
 *
 * \param item An item.
 *
 * \return The name.
 **/
gchar const*
j_item_get_name (JItem* item)
{
	g_return_val_if_fail(item != NULL, NULL);

	j_trace_enter(G_STRFUNC);
	j_trace_leave(G_STRFUNC);

	return item->name;
}

/**
 * Creates an item in a collection.
 *
 * \author Michael Kuhn
 *
 * \code
 * \endcode
 *
 * \param collection   A collection.
 * \param name         A name.
 * \param distribution A distribution.
 * \param batch        A batch.
 *
 * \return A new item. Should be freed with j_item_unref().
 **/
JItem*
j_item_create (JCollection* collection, gchar const* name, JDistribution* distribution, JBatch* batch)
{
	JItem* item;
	JOperation* operation;

	g_return_val_if_fail(collection != NULL, NULL);
	g_return_val_if_fail(name != NULL, NULL);

	j_trace_enter(G_STRFUNC);

	if ((item = j_item_new(collection, name, distribution)) == NULL)
	{
		goto end;
	}

	operation = j_operation_new(J_OPERATION_ITEM_CREATE);
	operation->key = collection;
	operation->u.item_create.collection = j_collection_ref(collection);
	operation->u.item_create.item = j_item_ref(item);

	j_batch_add(batch, operation);

end:
	j_trace_leave(G_STRFUNC);

	return item;
}

/**
 * Gets an item from a collection.
 *
 * \author Michael Kuhn
 *
 * \code
 * \endcode
 *
 * \param collection A collection.
 * \param item       A pointer to an item.
 * \param name       A name.
 * \param batch      A batch.
 **/
void
j_item_get (JCollection* collection, JItem** item, gchar const* name, JBatch* batch)
{
	JOperation* operation;

	g_return_if_fail(collection != NULL);
	g_return_if_fail(item != NULL);
	g_return_if_fail(name != NULL);

	j_trace_enter(G_STRFUNC);

	operation = j_operation_new(J_OPERATION_ITEM_GET);
	operation->key = collection;
	operation->u.item_get.collection = j_collection_ref(collection);
	operation->u.item_get.item = item;
	operation->u.item_get.name = g_strdup(name);

	j_batch_add(batch, operation);

	j_trace_leave(G_STRFUNC);
}

/**
 * Deletes an item from a collection.
 *
 * \author Michael Kuhn
 *
 * \code
 * \endcode
 *
 * \param collection A collection.
 * \param item       An item.
 * \param batch      A batch.
 **/
void
j_item_delete (JCollection* collection, JItem* item, JBatch* batch)
{
	JOperation* operation;

	g_return_if_fail(collection != NULL);
	g_return_if_fail(item != NULL);

	j_trace_enter(G_STRFUNC);

	operation = j_operation_new(J_OPERATION_ITEM_DELETE);
	operation->key = collection;
	operation->u.item_delete.collection = j_collection_ref(collection);
	operation->u.item_delete.item = j_item_ref(item);

	j_batch_add(batch, operation);

	j_trace_leave(G_STRFUNC);
}

/**
 * Reads an item.
 *
 * \author Michael Kuhn
 *
 * \code
 * \endcode
 *
 * \param item       An item.
 * \param data       A buffer to hold the read data.
 * \param length     Number of bytes to read.
 * \param offset     An offset within #item.
 * \param bytes_read Number of bytes read.
 * \param batch      A batch.
 **/
void
j_item_read (JItem* item, gpointer data, guint64 length, guint64 offset, guint64* bytes_read, JBatch* batch)
{
	JOperation* operation;

	g_return_if_fail(item != NULL);
	g_return_if_fail(data != NULL);
	g_return_if_fail(bytes_read != NULL);

	j_trace_enter(G_STRFUNC);

	operation = j_operation_new(J_OPERATION_ITEM_READ);
	operation->key = item;
	operation->u.item_read.item = j_item_ref(item);
	operation->u.item_read.data = data;
	operation->u.item_read.length = length;
	operation->u.item_read.offset = offset;
	operation->u.item_read.bytes_read = bytes_read;

	j_batch_add(batch, operation);

	j_trace_leave(G_STRFUNC);
}

/**
 * Writes an item.
 *
 * \note
 * j_item_write() modifies bytes_written even if j_batch_execute() is not called.
 *
 * \author Michael Kuhn
 *
 * \code
 * \endcode
 *
 * \param item          An item.
 * \param data          A buffer holding the data to write.
 * \param length        Number of bytes to write.
 * \param offset        An offset within #item.
 * \param bytes_written Number of bytes written.
 * \param batch         A batch.
 **/
void
j_item_write (JItem* item, gconstpointer data, guint64 length, guint64 offset, guint64* bytes_written, JBatch* batch)
{
	JOperation* operation;

	g_return_if_fail(item != NULL);
	g_return_if_fail(data != NULL);
	g_return_if_fail(bytes_written != NULL);

	j_trace_enter(G_STRFUNC);

	operation = j_operation_new(J_OPERATION_ITEM_WRITE);
	operation->key = item;
	operation->u.item_write.item = j_item_ref(item);
	operation->u.item_write.data = data;
	operation->u.item_write.length = length;
	operation->u.item_write.offset = offset;
	operation->u.item_write.bytes_written = bytes_written;

	*bytes_written = 0;

	j_batch_add(batch, operation);

	j_trace_leave(G_STRFUNC);
}

/**
 * Get the status of an item.
 *
 * \author Michael Kuhn
 *
 * \code
 * \endcode
 *
 * \param item      An item.
 * \param flags     Status flags.
 * \param batch     A batch.
 **/
void
j_item_get_status (JItem* item, JItemStatusFlags flags, JBatch* batch)
{
	JOperation* operation;

	g_return_if_fail(item != NULL);

	j_trace_enter(G_STRFUNC);

	operation = j_operation_new(J_OPERATION_ITEM_GET_STATUS);
	operation->key = item->collection;
	operation->u.item_get_status.item = j_item_ref(item);
	operation->u.item_get_status.flags = flags;

	j_batch_add(batch, operation);

	j_trace_leave(G_STRFUNC);
}

/**
 * Returns an item's size.
 *
 * \author Michael Kuhn
 *
 * \code
 * \endcode
 *
 * \param item An item.
 *
 * \return A size.
 **/
guint64
j_item_get_size (JItem* item)
{
	g_return_val_if_fail(item != NULL, 0);

	j_trace_enter(G_STRFUNC);
	j_trace_leave(G_STRFUNC);

	return item->status.size;
}

/**
 * Returns an item's modification time.
 *
 * \author Michael Kuhn
 *
 * \code
 * \endcode
 *
 * \param item An item.
 *
 * \return A modification time.
 **/
gint64
j_item_get_modification_time (JItem* item)
{
	g_return_val_if_fail(item != NULL, 0);

	j_trace_enter(G_STRFUNC);
	j_trace_leave(G_STRFUNC);

	return item->status.modification_time;
}

/**
 * Returns the item's optimal access size.
 *
 * \author Michael Kuhn
 *
 * \code
 * JItem* item;
 * guint64 optimal_size;
 *
 * ...
 * optimal_size = j_item_get_optimal_access_size(item);
 * j_item_write(item, buffer, optimal_size, 0, &dummy, batch);
 * ...
 * \endcode
 *
 * \param item An item.
 *
 * \return An access size.
 */
guint64
j_item_get_optimal_access_size (JItem* item)
{
	g_return_val_if_fail(item != NULL, 0);

	j_trace_enter(G_STRFUNC);
	j_trace_leave(G_STRFUNC);

	return J_KIB(512);
}

/* Internal */

/**
 * Creates a new item.
 *
 * \author Michael Kuhn
 *
 * \code
 * JItem* i;
 *
 * i = j_item_new("JULEA");
 * \endcode
 *
 * \param collection   A collection.
 * \param name         An item name.
 * \param distribution A distribution.
 *
 * \return A new item. Should be freed with j_item_unref().
 **/
JItem*
j_item_new (JCollection* collection, gchar const* name, JDistribution* distribution)
{
	JItem* item = NULL;

	g_return_val_if_fail(collection != NULL, NULL);
	g_return_val_if_fail(name != NULL, NULL);

	j_trace_enter(G_STRFUNC);

	if (strpbrk(name, "/") != NULL)
	{
		goto end;
	}

	if (distribution == NULL)
	{
		distribution = j_distribution_new(J_DISTRIBUTION_ROUND_ROBIN);
	}

	item = g_slice_new(JItem);
	bson_oid_init(&(item->id), bson_context_get_default());
	item->name = g_strdup(name);
	item->credentials = j_credentials_new();
	item->distribution = distribution;
	item->status.age = g_get_real_time();
	item->status.size = 0;
	item->status.modification_time = g_get_real_time();
	item->status.created = NULL;
	item->collection = j_collection_ref(collection);
	item->ref_count = 1;

end:
	j_trace_leave(G_STRFUNC);

	return item;
}

/**
 * Creates a new item from a BSON object.
 *
 * \private
 *
 * \author Michael Kuhn
 *
 * \code
 * \endcode
 *
 * \param collection A collection.
 * \param b          A BSON object.
 *
 * \return A new item. Should be freed with j_item_unref().
 **/
JItem*
j_item_new_from_bson (JCollection* collection, bson_t const* b)
{
	JItem* item;

	g_return_val_if_fail(collection != NULL, NULL);
	g_return_val_if_fail(b != NULL, NULL);

	j_trace_enter(G_STRFUNC);

	item = g_slice_new(JItem);
	item->name = NULL;
	item->credentials = j_credentials_new();
	item->distribution = NULL;
	item->status.age = 0;
	item->status.size = 0;
	item->status.modification_time = 0;
	item->status.created = NULL;
	item->collection = j_collection_ref(collection);
	item->ref_count = 1;

	j_item_deserialize(item, b);

	j_trace_leave(G_STRFUNC);

	return item;
}

/**
 * Returns an item's collection.
 *
 * \private
 *
 * \author Michael Kuhn
 *
 * \code
 * \endcode
 *
 * \param item An item.
 *
 * \return A collection.
 **/
JCollection*
j_item_get_collection (JItem* item)
{
	g_return_val_if_fail(item != NULL, NULL);

	j_trace_enter(G_STRFUNC);
	j_trace_leave(G_STRFUNC);

	return item->collection;
}

/**
 * Returns an item's credentials.
 *
 * \private
 *
 * \author Michael Kuhn
 *
 * \code
 * \endcode
 *
 * \param item An item.
 *
 * \return A collection.
 **/
JCredentials*
j_item_get_credentials (JItem* item)
{
	g_return_val_if_fail(item != NULL, NULL);

	j_trace_enter(G_STRFUNC);
	j_trace_leave(G_STRFUNC);

	return item->credentials;
}

/**
 * Serializes an item.
 *
 * \private
 *
 * \author Michael Kuhn
 *
 * \code
 * \endcode
 *
 * \param item      An item.
 * \param semantics A semantics object.
 *
 * \return A new BSON object. Should be freed with g_slice_free().
 **/
bson_t*
j_item_serialize (JItem* item, JSemantics* semantics)
{
	bson_t* b;
	bson_t* b_cred;
	bson_t* b_distribution;

	g_return_val_if_fail(item != NULL, NULL);

	j_trace_enter(G_STRFUNC);

	b_cred = j_credentials_serialize(item->credentials);
	b_distribution = j_distribution_serialize(item->distribution);

	b = g_slice_new(bson_t);
	bson_init(b);

	bson_append_oid(b, "_id", -1, &(item->id));
	bson_append_oid(b, "collection", -1, j_collection_get_id(item->collection));
	bson_append_utf8(b, "name", -1, item->name, -1);

	if (j_semantics_get(semantics, J_SEMANTICS_CONCURRENCY) == J_SEMANTICS_CONCURRENCY_NONE)
	{
		bson_t b_document[1];

		bson_append_document_begin(b, "status", -1, b_document);

		bson_append_int64(b_document, "size", -1, item->status.size);
		bson_append_int64(b_document, "modification_time", -1, item->status.modification_time);

		bson_append_document_end(b, b_document);

		bson_destroy(b_document);
	}

	bson_append_document(b, "credentials", -1, b_cred);
	bson_append_document(b, "distribution", -1, b_distribution);

	//bson_finish(b);

	bson_destroy(b_cred);
	bson_destroy(b_distribution);
	g_slice_free(bson_t, b_cred);
	g_slice_free(bson_t, b_distribution);

	j_trace_leave(G_STRFUNC);

	return b;
}

static
void
j_item_deserialize_status (JItem* item, bson_t const* b)
{
	bson_iter_t iterator;

	g_return_if_fail(item != NULL);
	g_return_if_fail(b != NULL);

	j_trace_enter(G_STRFUNC);

	bson_iter_init(&iterator, b);

	while (bson_iter_next(&iterator))
	{
		gchar const* key;

		key = bson_iter_key(&iterator);

		if (g_strcmp0(key, "size") == 0)
		{
			item->status.size = bson_iter_int64(&iterator);
			item->status.age = g_get_real_time();
		}
		else if (g_strcmp0(key, "modification_time") == 0)
		{
			item->status.modification_time = bson_iter_int64(&iterator);
			item->status.age = g_get_real_time();
		}
	}

	j_trace_leave(G_STRFUNC);
}

/**
 * Deserializes an item.
 *
 * \private
 *
 * \author Michael Kuhn
 *
 * \code
 * \endcode
 *
 * \param item An item.
 * \param b    A BSON object.
 **/
void
j_item_deserialize (JItem* item, bson_t const* b)
{
	bson_iter_t iterator;

	g_return_if_fail(item != NULL);
	g_return_if_fail(b != NULL);

	j_trace_enter(G_STRFUNC);

	bson_iter_init(&iterator, b);

	while (bson_iter_next(&iterator))
	{
		gchar const* key;

		key = bson_iter_key(&iterator);

		if (g_strcmp0(key, "_id") == 0)
		{
			item->id = *bson_iter_oid(&iterator);
		}
		else if (g_strcmp0(key, "name") == 0)
		{
			g_free(item->name);
			item->name = g_strdup(bson_iter_utf8(&iterator, NULL /*FIXME*/));
		}
		else if (g_strcmp0(key, "status") == 0)
		{
			guint8 const* data;
			guint32 len;
			bson_t b_status[1];

			bson_iter_document(&iterator, &len, &data);
			bson_init_static(b_status, data, len);
			j_item_deserialize_status(item, b_status);
			bson_destroy(b_status);
		}
		else if (g_strcmp0(key, "credentials") == 0)
		{
			guint8 const* data;
			guint32 len;
			bson_t b_cred[1];

			bson_iter_document(&iterator, &len, &data);
			bson_init_static(b_cred, data, len);
			j_credentials_deserialize(item->credentials, b_cred);
			bson_destroy(b_cred);
		}
		else if (g_strcmp0(key, "distribution") == 0)
		{
			guint8 const* data;
			guint32 len;
			bson_t b_distribution[1];

			if (item->distribution != NULL)
			{
				j_distribution_unref(item->distribution);
			}

			bson_iter_document(&iterator, &len, &data);
			bson_init_static(b_distribution, data, len);
			item->distribution = j_distribution_new_from_bson(b_distribution);
			bson_destroy(b_distribution);
		}
	}

	j_trace_leave(G_STRFUNC);
}

/**
 * Returns an item's ID.
 *
 * \private
 *
 * \author Michael Kuhn
 *
 * \code
 * \endcode
 *
 * \param item An item.
 *
 * \return An ID.
 **/
bson_oid_t const*
j_item_get_id (JItem* item)
{
	g_return_val_if_fail(item != NULL, NULL);

	j_trace_enter(G_STRFUNC);
	j_trace_leave(G_STRFUNC);

	return &(item->id);
}

gboolean
j_item_create_internal (JBatch* batch, JList* operations)
{
	JBackend* meta_backend;
	JCollection* collection = NULL;
	JListIterator* it;
	JSemantics* semantics;
	gboolean ret = FALSE;
	gpointer meta_batch;

	g_return_val_if_fail(batch != NULL, FALSE);
	g_return_val_if_fail(operations != NULL, FALSE);

	j_trace_enter(G_STRFUNC);

	semantics = j_batch_get_semantics(batch);

	it = j_list_iterator_new(operations);

	meta_backend = j_metadata_backend();
	//mongo_connection = j_connection_pool_pop_meta(0);

	if (meta_backend != NULL)
	{
		ret = meta_backend->u.meta.batch_start("items", &meta_batch);
	}

	while (j_list_iterator_next(it))
	{
		JOperation* operation = j_list_iterator_get(it);
		JItem* item = operation->u.item_create.item;
		bson_t* b;
		gchar* path;

		collection = operation->u.item_create.collection;

		if (collection == NULL)
		{
			continue;
		}

		b = j_item_serialize(item, semantics);

		if (meta_backend != NULL)
		{
			path = g_build_path("/", j_collection_get_name(item->collection), item->name, NULL);
			ret = meta_backend->u.meta.create(path, b, meta_batch) && ret;
			g_free(path);
		}

		bson_destroy(b);
		g_slice_free(bson_t, b);
	}

	if (meta_backend != NULL)
	{
		ret = meta_backend->u.meta.batch_execute(meta_batch) && ret;
	}

	j_list_iterator_free(it);

	//j_connection_pool_push_meta(0, mongo_connection);

	j_trace_leave(G_STRFUNC);

	return ret;
}

gboolean
j_item_delete_internal (JBatch* batch, JList* operations)
{
	JBackend* meta_backend;
	JBackgroundOperation** background_operations;
	JCollection* collection = NULL;
	JListIterator* it;
	JMessage** messages;
	JSemantics* semantics;
	gboolean ret = TRUE;
	guint m;
	guint n;
	gchar const* item_name;
	gchar const* collection_name;
	gpointer meta_batch;

	g_return_val_if_fail(batch != NULL, FALSE);
	g_return_val_if_fail(operations != NULL, FALSE);

	j_trace_enter(G_STRFUNC);

	/*
		IsInitialized(true);
	*/

	semantics = j_batch_get_semantics(batch);
	n = j_configuration_get_data_server_count(j_configuration());
	background_operations = g_new(JBackgroundOperation*, n);
	messages = g_new(JMessage*, n);

	for (guint i = 0; i < n; i++)
	{
		background_operations[i] = NULL;
		messages[i] = NULL;
	}

	{
		JOperation* operation;

		operation = j_list_get_first(operations);
		g_assert(operation != NULL);
		collection = operation->u.item_delete.collection;

		collection_name = j_collection_get_name(collection);
	}

	it = j_list_iterator_new(operations);
	//mongo_connection = j_connection_pool_pop_meta(0);
	meta_backend = j_metadata_backend();

	if (meta_backend != NULL)
	{
		ret = meta_backend->u.meta.batch_start("items", &meta_batch);
	}

	/* FIXME do some optimizations for len(operations) > 1 */
	while (j_list_iterator_next(it))
	{
		JOperation* operation = j_list_iterator_get(it);
		JItem* item = operation->u.item_delete.item;
		gchar* path;
		gsize path_len;

		item_name = j_item_get_name(item);

		path = g_build_path("/", collection_name, item_name, NULL);
		path_len = strlen(path) + 1;

		if (meta_backend != NULL)
		{
			ret = meta_backend->u.meta.delete(path, meta_batch) && ret;
		}

		//bson_init(&b);
		//bson_append_oid(&b, "_id", -1, j_item_get_id(item));
		//bson_finish(&b);

		for (guint i = 0; i < n; i++)
		{
			if (messages[i] == NULL)
			{
				/* FIXME */
				messages[i] = j_message_new(J_MESSAGE_DATA_DELETE, 0);
				j_message_set_safety(messages[i], semantics);
			}

			j_message_add_operation(messages[i], path_len);
			j_message_append_n(messages[i], path, path_len);

			g_free(path);
		}
	}

	if (meta_backend != NULL)
	{
		ret = meta_backend->u.meta.batch_execute(meta_batch) && ret;
	}

	//j_connection_pool_push_meta(0, mongo_connection);

	m = 0;

	for (guint i = 0; i < n; i++)
	{
		if (messages[i] != NULL)
		{
			m++;
		}
	}

	for (guint i = 0; i < n; i++)
	{
		JCollectionBackgroundData* background_data;

		if (messages[i] == NULL)
		{
			continue;
		}

		background_data = g_slice_new(JCollectionBackgroundData);
		background_data->connection = j_connection_pool_pop_data(i);
		background_data->message = messages[i];

		if (m > 1)
		{
			background_operations[i] = j_background_operation_new(j_item_delete_background_operation, background_data);
		}
		else
		{
			j_item_delete_background_operation(background_data);

			j_connection_pool_push_data(i, background_data->connection);

			g_slice_free(JCollectionBackgroundData, background_data);
		}
	}

	for (guint i = 0; i < n; i++)
	{
		if (background_operations[i] != NULL)
		{
			JCollectionBackgroundData* background_data;

			background_data = j_background_operation_wait(background_operations[i]);
			j_background_operation_unref(background_operations[i]);

			j_connection_pool_push_data(i, background_data->connection);

			g_slice_free(JCollectionBackgroundData, background_data);
		}

		if (messages[i] != NULL)
		{
			j_message_unref(messages[i]);
		}
	}

	j_list_iterator_free(it);

	g_free(background_operations);
	g_free(messages);

	j_trace_leave(G_STRFUNC);

	return ret;
}

gboolean
j_item_get_internal (JBatch* batch, JList* operations)
{
	JBackend* meta_backend;
	JListIterator* it;
	gboolean ret = TRUE;

	g_return_val_if_fail(batch != NULL, FALSE);
	g_return_val_if_fail(operations != NULL, FALSE);

	j_trace_enter(G_STRFUNC);

	it = j_list_iterator_new(operations);
	//mongo_connection = j_connection_pool_pop_meta(0);
	meta_backend = j_metadata_backend();

	/* FIXME do some optimizations for len(operations) > 1 */
	while (j_list_iterator_next(it))
	{
		JOperation* operation = j_list_iterator_get(it);
		JCollection* collection = operation->u.item_get.collection;
		JItem** item = operation->u.item_get.item;
		bson_t result[1];
		gchar const* name = operation->u.item_get.name;
		gchar* path;

		if (meta_backend != NULL)
		{
			path = g_build_path("/", j_collection_get_name(collection), name, NULL);
			ret = meta_backend->u.meta.get("items", path, result) && ret;
			g_free(path);
		}

		*item = NULL;

		if (ret)
		{
			*item = j_item_new_from_bson(collection, result);
			bson_destroy(result);
		}
	}

	//j_connection_pool_push_meta(0, mongo_connection);
	j_list_iterator_free(it);

	j_trace_leave(G_STRFUNC);

	return ret;
}

/**
 * Sets an item's modification time.
 *
 * \author Michael Kuhn
 *
 * \code
 * \endcode
 *
 * \param item              An item.
 * \param modification_time A modification time.
 **/
void
j_item_set_modification_time (JItem* item, gint64 modification_time)
{
	g_return_if_fail(item != NULL);

	j_trace_enter(G_STRFUNC);
	item->status.age = g_get_real_time();
	item->status.modification_time = MAX(item->status.modification_time, modification_time);
	j_trace_leave(G_STRFUNC);
}

/**
 * Sets an item's size.
 *
 * \author Michael Kuhn
 *
 * \code
 * \endcode
 *
 * \param item An item.
 * \param size A size.
 **/
void
j_item_set_size (JItem* item, guint64 size)
{
	g_return_if_fail(item != NULL);

	j_trace_enter(G_STRFUNC);
	item->status.age = g_get_real_time();
	item->status.size = size;
	j_trace_leave(G_STRFUNC);
}

gboolean
j_item_read_internal (JBatch* batch, JList* operations)
{
	JBackgroundOperation** background_operations;
	JItem* item;
	JList** buffer_list;
	JListIterator* iterator;
	JLock* lock = NULL;
	JMessage** messages;
	JSemantics* semantics;
	guint m;
	guint n;
	gchar const* item_name;
	gchar const* collection_name;

	g_return_val_if_fail(batch != NULL, FALSE);
	g_return_val_if_fail(operations != NULL, FALSE);

	j_trace_enter(G_STRFUNC);

	semantics = j_batch_get_semantics(batch);
	n = j_configuration_get_data_server_count(j_configuration());
	background_operations = g_new(JBackgroundOperation*, n);
	messages = g_new(JMessage*, n);
	buffer_list = g_new(JList*, n);

	for (guint i = 0; i < n; i++)
	{
		background_operations[i] = NULL;
		messages[i] = NULL;
		buffer_list[i] = j_list_new(NULL);
	}

	{
		JOperation* operation;

		operation = j_list_get_first(operations);
		g_assert(operation != NULL);
		item = operation->u.item_read.item;

		item_name = item->name;
		collection_name = j_collection_get_name(item->collection);
	}

	if (j_semantics_get(semantics, J_SEMANTICS_ATOMICITY) != J_SEMANTICS_ATOMICITY_NONE)
	{
		lock = j_lock_new(item);
	}

	iterator = j_list_iterator_new(operations);

	while (j_list_iterator_next(iterator))
	{
		JOperation* operation = j_list_iterator_get(iterator);
		gpointer data = operation->u.item_read.data;
		guint64 length = operation->u.item_read.length;
		guint64 offset = operation->u.item_read.offset;
		guint64* bytes_read = operation->u.item_read.bytes_read;

		JItemReadData* buffer;
		gchar* d;
		guint64 new_length;
		guint64 new_offset;
		guint64 block_id;
		guint index;

		*bytes_read = 0;

		if (length == 0)
		{
			continue;
		}

		j_trace_file_begin(item_name, J_TRACE_FILE_READ);

		j_distribution_reset(item->distribution, length, offset);
		d = data;

		while (j_distribution_distribute(item->distribution, &index, &new_length, &new_offset, &block_id))
		{
			if (messages[index] == NULL)
			{
				gchar* path;
				gsize path_len;

				path = g_build_path("/", collection_name, item_name, NULL);
				path_len = strlen(path) + 1;

				messages[index] = j_message_new(J_MESSAGE_DATA_READ, path_len);
				j_message_append_n(messages[index], path, path_len);

				g_free(path);
			}

			j_message_add_operation(messages[index], sizeof(guint64) + sizeof(guint64));
			j_message_append_8(messages[index], &new_length);
			j_message_append_8(messages[index], &new_offset);

			buffer = g_slice_new(JItemReadData);
			buffer->data = d;
			buffer->nbytes = bytes_read;

			j_list_append(buffer_list[index], buffer);

			if (lock != NULL)
			{
				j_lock_add(lock, block_id);
			}

			d += new_length;
		}

		j_trace_file_end(item_name, J_TRACE_FILE_READ, length, offset);
	}

	j_list_iterator_free(iterator);

	if (lock != NULL)
	{
		/* FIXME busy wait */
		while (!j_lock_acquire(lock));
	}

	// FIXME ugly
	m = 0;

	for (guint i = 0; i < n; i++)
	{
		if (messages[i] != NULL)
		{
			m++;
		}
	}

	for (guint i = 0; i < n; i++)
	{
		JItemBackgroundData* background_data;

		if (messages[i] == NULL)
		{
			continue;
		}

		background_data = g_slice_new(JItemBackgroundData);
		background_data->connection = j_connection_pool_pop_data(i);
		background_data->message = messages[i];
		background_data->index = i;
		background_data->u.read.buffer_list = buffer_list[i];

		// FIXME ugly
		if (m > 1)
		{
			background_operations[i] = j_background_operation_new(j_item_read_background_operation, background_data);
		}
		else
		{
			j_item_read_background_operation(background_data);

			j_connection_pool_push_data(i, background_data->connection);

			g_slice_free(JItemBackgroundData, background_data);
		}
	}

	for (guint i = 0; i < n; i++)
	{
		if (background_operations[i] != NULL)
		{
			JItemBackgroundData* background_data;

			background_data = j_background_operation_wait(background_operations[i]);
			j_background_operation_unref(background_operations[i]);

			j_connection_pool_push_data(i, background_data->connection);

			g_slice_free(JItemBackgroundData, background_data);
		}

		if (messages[i] != NULL)
		{
			j_message_unref(messages[i]);
		}

		j_list_unref(buffer_list[i]);
	}

	if (lock != NULL)
	{
		j_lock_free(lock);
	}

	g_free(background_operations);
	g_free(messages);
	g_free(buffer_list);

	j_trace_leave(G_STRFUNC);

	return TRUE;
}

gboolean
j_item_write_internal (JBatch* batch, JList* operations)
{
	JBackgroundOperation** background_operations;
	JItem* item;
	JListIterator* iterator;
	JLock* lock = NULL;
	JMessage** messages;
	JSemantics* semantics;
	guint m;
	guint n;
	gchar const* item_name;
	gchar const* collection_name;
	guint64 max_offset = 0;

	g_return_val_if_fail(batch != NULL, FALSE);
	g_return_val_if_fail(operations != NULL, FALSE);

	j_trace_enter(G_STRFUNC);

	semantics = j_batch_get_semantics(batch);
	n = j_configuration_get_data_server_count(j_configuration());
	background_operations = g_new(JBackgroundOperation*, n);
	messages = g_new(JMessage*, n);

	for (guint i = 0; i < n; i++)
	{
		background_operations[i] = NULL;
		messages[i] = NULL;
	}

	{
		JOperation* operation;

		operation = j_list_get_first(operations);
		g_assert(operation != NULL);
		item = operation->u.item_write.item;

		item_name = item->name;
		collection_name = j_collection_get_name(item->collection);
	}

	if (j_semantics_get(semantics, J_SEMANTICS_ATOMICITY) != J_SEMANTICS_ATOMICITY_NONE)
	{
		lock = j_lock_new(item);
	}

	iterator = j_list_iterator_new(operations);

	while (j_list_iterator_next(iterator))
	{
		JOperation* operation = j_list_iterator_get(iterator);
		gconstpointer data = operation->u.item_write.data;
		guint64 length = operation->u.item_write.length;
		guint64 offset = operation->u.item_write.offset;
		guint64* bytes_written = operation->u.item_write.bytes_written;

		gchar const* d;
		guint64 new_length;
		guint64 new_offset;
		guint64 block_id;
		guint index;

		if (length == 0)
		{
			continue;
		}

		max_offset = MAX(max_offset, offset + length);

		j_trace_file_begin(item_name, J_TRACE_FILE_WRITE);

		j_distribution_reset(item->distribution, length, offset);
		d = data;

		while (j_distribution_distribute(item->distribution, &index, &new_length, &new_offset, &block_id))
		{
			if (messages[index] == NULL)
			{
				gchar* path;
				gsize path_len;

				path = g_build_path("/", collection_name, item_name, NULL);
				path_len = strlen(path) + 1;

				/* FIXME */
				messages[index] = j_message_new(J_MESSAGE_DATA_WRITE, path_len);
				j_message_set_safety(messages[index], semantics);
				j_message_append_n(messages[index], path, path_len);

				g_free(path);
			}

			j_message_add_operation(messages[index], sizeof(guint64) + sizeof(guint64));
			j_message_append_8(messages[index], &new_length);
			j_message_append_8(messages[index], &new_offset);
			j_message_add_send(messages[index], d, new_length);

			if (lock != NULL)
			{
				j_lock_add(lock, block_id);
			}

			d += new_length;

			/* FIXME */
			if (bytes_written != NULL)
			{
				*bytes_written += new_length;
			}
		}

		j_trace_file_end(item_name, J_TRACE_FILE_WRITE, length, offset);
	}

	j_list_iterator_free(iterator);

	if (lock != NULL)
	{
		/* FIXME busy wait */
		while (!j_lock_acquire(lock));
	}

	m = 0;

	for (guint i = 0; i < n; i++)
	{
		if (messages[i] != NULL)
		{
			m++;
		}
	}

	for (guint i = 0; i < n; i++)
	{
		JItemBackgroundData* background_data;
		JMessage* create_message = NULL;

		if (messages[i] == NULL)
		{
			continue;
		}

		if (item->status.created == NULL)
		{
			item->status.created = g_new(gboolean, n);

			for (guint j = 0; j < n; j++)
			{
				item->status.created[j] = FALSE;
			}
		}

		if (!item->status.created[i])
		{
			gchar* path;
			gsize path_len;

			path = g_build_path("/", collection_name, item_name, NULL);
			path_len = strlen(path) + 1;

			/* FIXME better solution? */
			create_message = j_message_new(J_MESSAGE_DATA_CREATE, 0);
			/**
			 * Force safe semantics to make the server send a reply.
			 * Otherwise, nasty races can occur when using unsafe semantics:
			 * - The client creates the item and sends its first write.
			 * - The client sends another operation using another connection from the pool.
			 * - The second operation is executed first and fails because the item does not exist.
			 * This does not completely eliminate all races but fixes the common case of create, write, write, ...
			 **/
			j_message_force_safety(create_message, J_SEMANTICS_SAFETY_NETWORK);
			j_message_add_operation(create_message, path_len);
			j_message_append_n(create_message, path, path_len);

			g_free(path);

			item->status.created[i] = TRUE;
		}

		background_data = g_slice_new(JItemBackgroundData);
		background_data->connection = j_connection_pool_pop_data(i);
		background_data->message = messages[i];
		background_data->index = i;
		background_data->u.write.create_message = create_message;

		if (m > 1)
		{
			background_operations[i] = j_background_operation_new(j_item_write_background_operation, background_data);
		}
		else
		{
			j_item_write_background_operation(background_data);

			if (create_message != NULL)
			{
				j_message_unref(create_message);
			}

			j_connection_pool_push_data(i, background_data->connection);

			g_slice_free(JItemBackgroundData, background_data);
		}
	}

	for (guint i = 0; i < n; i++)
	{
		if (background_operations[i] != NULL)
		{
			JItemBackgroundData* background_data;

			background_data = j_background_operation_wait(background_operations[i]);
			j_background_operation_unref(background_operations[i]);

			if (background_data->u.write.create_message != NULL)
			{
				j_message_unref(background_data->u.write.create_message);
			}

			j_connection_pool_push_data(i, background_data->connection);

			g_slice_free(JItemBackgroundData, background_data);
		}

		if (messages[i] != NULL)
		{
			j_message_unref(messages[i]);
		}
	}

	/*
	if (j_semantics_get(semantics, J_SEMANTICS_CONCURRENCY) == J_SEMANTICS_CONCURRENCY_NONE && FALSE)
	{
		bson_t b_document[1];
		bson_t cond[1];
		bson_t op[1];
		mongoc_client_t* mongo_connection;
		mongoc_collection_t* mongo_collection;
		mongoc_write_concern_t* write_concern;
		gint ret;

		j_helper_set_write_concern(write_concern, semantics);

		bson_init(cond);
		bson_append_oid(cond, "_id", -1, &(item->id));
		bson_append_int32(cond, "Status.ModificationTime", -1, item->status.modification_time);
		//bson_finish(cond);

		j_item_set_modification_time(item, g_get_real_time());

		bson_init(op);

		bson_append_document_begin(op, "$set", -1, b_document);

		if (max_offset > item->status.size)
		{
			j_item_set_size(item, max_offset);
			bson_append_int64(b_document, "Status.Size", -1, item->status.size);
		}

		bson_append_int64(b_document, "Status.ModificationTime", -1, item->status.modification_time);
		bson_append_document_end(op, b_document);

		//bson_finish(op);

		mongo_connection = j_connection_pool_pop_meta(0);
		mongo_collection = mongoc_client_get_collection(mongo_connection, "JULEA", "Items");

		ret = mongoc_collection_update(mongo_collection, MONGOC_UPDATE_NONE, cond, op, write_concern, NULL);

		j_connection_pool_push_meta(0, mongo_connection);

		if (!ret)
		{

		}

		bson_destroy(cond);
		bson_destroy(op);

		mongoc_write_concern_destroy(write_concern);
	}
	*/

	if (lock != NULL)
	{
		j_lock_free(lock);
	}

	g_free(background_operations);
	g_free(messages);

	j_trace_leave(G_STRFUNC);

	return TRUE;
}

gboolean
j_item_get_status_internal (JBatch* batch, JList* operations)
{
	gboolean ret = TRUE;
	JBackend* meta_backend;
	JBackgroundOperation** background_operations;
	JList* buffer_list;
	JListIterator* iterator;
	JMessage** messages;
	JSemantics* semantics;
	gint semantics_concurrency;
	gint semantics_consistency;
	guint n;

	g_return_val_if_fail(batch != NULL, FALSE);
	g_return_val_if_fail(operations != NULL, FALSE);

	j_trace_enter(G_STRFUNC);

	n = j_configuration_get_data_server_count(j_configuration());
	background_operations = g_new(JBackgroundOperation*, n);
	messages = g_new(JMessage*, n);
	buffer_list = j_list_new(NULL);

	for (guint i = 0; i < n; i++)
	{
		background_operations[i] = NULL;
		messages[i] = NULL;
	}

	iterator = j_list_iterator_new(operations);
	//mongo_connection = j_connection_pool_pop_meta(0);
	semantics = j_batch_get_semantics(batch);
	semantics_concurrency = j_semantics_get(semantics, J_SEMANTICS_CONCURRENCY);
	semantics_consistency = j_semantics_get(semantics, J_SEMANTICS_CONSISTENCY);

	meta_backend = j_metadata_backend();

	while (j_list_iterator_next(iterator))
	{
		JOperation* operation = j_list_iterator_get(iterator);
		JItem* item = operation->u.item_get_status.item;
		JItemStatusFlags flags = operation->u.item_get_status.flags;

		if (flags == J_ITEM_STATUS_NONE)
		{
			continue;
		}

		if (semantics_consistency != J_SEMANTICS_CONSISTENCY_IMMEDIATE)
		{
			if (item->status.age >= (guint64)g_get_real_time() - G_USEC_PER_SEC)
			{
				continue;
			}
		}

		if (semantics_concurrency == J_SEMANTICS_CONCURRENCY_NONE)
		{
			bson_t result[1];
			gchar* path;

			/*
			bson_init(&opts);
			bson_append_int32(&opts, "limit", -1, 1);
			bson_append_document_begin(&opts, "projection", -1, &projection);

			if (flags & J_ITEM_STATUS_SIZE)
			{
				bson_append_bool(&projection, "Status.Size", -1, TRUE);
			}

			if (flags & J_ITEM_STATUS_MODIFICATION_TIME)
			{
				bson_append_bool(&projection, "Status.ModificationTime", -1, TRUE);
			}

			bson_append_document_end(&opts, &projection);
			*/

			if (meta_backend != NULL)
			{
				path = g_build_path("/", j_collection_get_name(item->collection), item->name, NULL);
				ret = meta_backend->u.meta.get("items", path, result) && ret;
				g_free(path);
			}

			/*
			bson_init(&b);
			bson_append_oid(&b, "_id", -1, &(item->id));
			*/

			if (ret)
			{
				j_item_deserialize(item, result);
				bson_destroy(result);
			}
		}
		else
		{
			JItemReadStatusData* buffer;

			gchar const* collection_name;

			collection_name = j_collection_get_name(item->collection);

			for (guint i = 0; i < n; i++)
			{
				gchar* path;
				gsize path_len;

				gchar const* item_name;

				item_name = item->name;

				path = g_build_path("/", collection_name, item_name, NULL);
				path_len = strlen(path) + 1;

				if (messages[i] == NULL)
				{
					messages[i] = j_message_new(J_MESSAGE_DATA_STATUS, 0);
				}

				j_message_add_operation(messages[i], path_len + sizeof(guint32));
				j_message_append_n(messages[i], path, path_len);
				j_message_append_4(messages[i], &flags);

				g_free(path);
			}

			buffer = g_slice_new(JItemReadStatusData);
			buffer->item = item;
			buffer->flags = flags;
			buffer->sizes = g_slice_alloc0(n * sizeof(guint64));

			j_list_append(buffer_list, buffer);
		}
	}

	j_list_iterator_free(iterator);
	//j_connection_pool_push_meta(0, mongo_connection);

	for (guint i = 0; i < n; i++)
	{
		JItemBackgroundData* background_data;

		if (messages[i] == NULL)
		{
			continue;
		}

		background_data = g_slice_new(JItemBackgroundData);
		background_data->connection = j_connection_pool_pop_data(i);
		background_data->message = messages[i];
		background_data->index = i;
		background_data->u.read_status.buffer_list = buffer_list;

		background_operations[i] = j_background_operation_new(j_item_get_status_background_operation, background_data);
	}

	for (guint i = 0; i < n; i++)
	{
		if (background_operations[i] != NULL)
		{
			JItemBackgroundData* background_data;

			background_data = j_background_operation_wait(background_operations[i]);
			j_background_operation_unref(background_operations[i]);

			j_connection_pool_push_data(i, background_data->connection);

			g_slice_free(JItemBackgroundData, background_data);
		}

		if (messages[i] != NULL)
		{
			j_message_unref(messages[i]);
		}

	}

	iterator = j_list_iterator_new(buffer_list);

	while (j_list_iterator_next(iterator))
	{
		JItemReadStatusData* buffer = j_list_iterator_get(iterator);
		guint64 size = 0;

		for (guint i = 0; i < n; i++)
		{
			size += buffer->sizes[i];
		}

		j_item_set_size(buffer->item, size);

		g_slice_free1(n * sizeof(guint64), buffer->sizes);
		g_slice_free(JItemReadStatusData, buffer);
	}

	j_list_iterator_free(iterator);

	j_list_unref(buffer_list);

	g_free(background_operations);
	g_free(messages);

	j_trace_leave(G_STRFUNC);

	return ret;
}

/**
 * @}
 **/
