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
#include <glib-object.h>
#include <gio/gio.h>

#include <jconnection-pool.h>
#include <jconnection-pool-internal.h>

#include <jhelper-internal.h>
#include <jmessage-internal.h>
#include <jtrace-internal.h>

/**
 * \defgroup JConnectionPool Connection Pool
 *
 * Data structures and functions for managing connection pools.
 *
 * @{
 **/

struct JConnectionPoolQueue
{
	GAsyncQueue* queue;
	guint count;
};

typedef struct JConnectionPoolQueue JConnectionPoolQueue;

/**
 * A connection.
 **/
struct JConnectionPool
{
	JConfiguration* configuration;
	JConnectionPoolQueue* data_queues;
	JConnectionPoolQueue* meta_queues;
	guint data_len;
	guint meta_len;
	guint max_count;
};

typedef struct JConnectionPool JConnectionPool;

static JConnectionPool* j_connection_pool = NULL;

void
j_connection_pool_init (JConfiguration* configuration)
{
	JConnectionPool* pool;

	g_return_if_fail(j_connection_pool == NULL);

	j_trace_enter(G_STRFUNC, NULL);

	pool = g_slice_new(JConnectionPool);
	pool->configuration = j_configuration_ref(configuration);
	pool->data_len = j_configuration_get_data_server_count(configuration);
	pool->data_queues = g_new(JConnectionPoolQueue, pool->data_len);
	pool->meta_len = j_configuration_get_metadata_server_count(configuration);
	pool->meta_queues = g_new(JConnectionPoolQueue, pool->meta_len);
	pool->max_count = j_configuration_get_max_connections(configuration);

	if (pool->max_count == 0)
	{
		pool->max_count = g_get_num_processors();
	}

	for (guint i = 0; i < pool->data_len; i++)
	{
		pool->data_queues[i].queue = g_async_queue_new();
		pool->data_queues[i].count = 0;
	}

	for (guint i = 0; i < pool->meta_len; i++)
	{
		pool->meta_queues[i].queue = g_async_queue_new();
		pool->meta_queues[i].count = 0;
	}

	g_atomic_pointer_set(&j_connection_pool, pool);

	j_trace_leave(G_STRFUNC);
}

void
j_connection_pool_fini (void)
{
	JConnectionPool* pool;

	g_return_if_fail(j_connection_pool != NULL);

	j_trace_enter(G_STRFUNC, NULL);

	pool = g_atomic_pointer_get(&j_connection_pool);
	g_atomic_pointer_set(&j_connection_pool, NULL);

	for (guint i = 0; i < pool->data_len; i++)
	{
		GSocketConnection* connection;

		while ((connection = g_async_queue_try_pop(pool->data_queues[i].queue)) != NULL)
		{
			g_io_stream_close(G_IO_STREAM(connection), NULL, NULL);
			g_object_unref(connection);
		}

		g_async_queue_unref(pool->data_queues[i].queue);
	}

	for (guint i = 0; i < pool->meta_len; i++)
	{
		GSocketConnection* connection;

		while ((connection = g_async_queue_try_pop(pool->meta_queues[i].queue)) != NULL)
		{
			g_io_stream_close(G_IO_STREAM(connection), NULL, NULL);
			g_object_unref(connection);
		}

		g_async_queue_unref(pool->meta_queues[i].queue);
	}

	j_configuration_unref(pool->configuration);

	g_free(pool->data_queues);
	g_free(pool->meta_queues);

	g_slice_free(JConnectionPool, pool);

	j_trace_leave(G_STRFUNC);
}

static
GSocketConnection*
j_connection_pool_pop_internal (GAsyncQueue* queue, guint* count, gchar const* server)
{
	GSocketConnection* connection;

	g_return_val_if_fail(queue != NULL, NULL);
	g_return_val_if_fail(count != NULL, NULL);

	j_trace_enter(G_STRFUNC, NULL);

	connection = g_async_queue_try_pop(queue);

	if (connection != NULL)
	{
		goto end;
	}

	if ((guint)g_atomic_int_get(count) < j_connection_pool->max_count)
	{
		if ((guint)g_atomic_int_add(count, 1) < j_connection_pool->max_count)
		{
			GError* error = NULL;
			g_autoptr(GSocketClient) client = NULL;

			g_autoptr(JMessage) message = NULL;
			g_autoptr(JMessage) reply = NULL;

			guint op_count;

			client = g_socket_client_new();
			connection = g_socket_client_connect_to_host(client, server, 4711, NULL, &error);

			if (error != NULL)
			{
				J_CRITICAL("%s", error->message);
				g_error_free(error);
			}

			if (connection == NULL)
			{
				J_CRITICAL("Can not connect to %s [%d].", server, g_atomic_int_get(count));
			}

			j_helper_set_nodelay(connection, TRUE);

			message = j_message_new(J_MESSAGE_PING, 0);
			j_message_send(message, connection);

			reply = j_message_new_reply(message);
			j_message_receive(reply, connection);

			op_count = j_message_get_count(reply);

			for (guint i = 0; i < op_count; i++)
			{
				gchar const* backend;

				backend = j_message_get_string(reply);

				if (g_strcmp0(backend, "data") == 0)
				{
					//g_print("Server has data backend.\n");
				}
				else if (g_strcmp0(backend, "meta") == 0)
				{
					//g_print("Server has metadata backend.\n");
				}
			}
		}
		else
		{
			g_atomic_int_add(count, -1);
		}
	}

	if (connection != NULL)
	{
		goto end;
	}

	connection = g_async_queue_pop(queue);

end:
	j_trace_leave(G_STRFUNC);

	return connection;
}

static
void
j_connection_pool_push_internal (GAsyncQueue* queue, GSocketConnection* connection)
{
	g_return_if_fail(queue != NULL);
	g_return_if_fail(connection != NULL);

	j_trace_enter(G_STRFUNC, NULL);

	g_async_queue_push(queue, connection);

	j_trace_leave(G_STRFUNC);
}

GSocketConnection*
j_connection_pool_pop_data (guint index)
{
	GSocketConnection* connection;

	g_return_val_if_fail(j_connection_pool != NULL, NULL);
	g_return_val_if_fail(index < j_connection_pool->data_len, NULL);

	j_trace_enter(G_STRFUNC, NULL);

	connection = j_connection_pool_pop_internal(j_connection_pool->data_queues[index].queue, &(j_connection_pool->data_queues[index].count), j_configuration_get_data_server(j_connection_pool->configuration, index));

	j_trace_leave(G_STRFUNC);

	return connection;
}

void
j_connection_pool_push_data (guint index, GSocketConnection* connection)
{
	g_return_if_fail(j_connection_pool != NULL);
	g_return_if_fail(index < j_connection_pool->data_len);
	g_return_if_fail(connection != NULL);

	j_trace_enter(G_STRFUNC, NULL);

	j_connection_pool_push_internal(j_connection_pool->data_queues[index].queue, connection);

	j_trace_leave(G_STRFUNC);
}

GSocketConnection*
j_connection_pool_pop_meta (guint index)
{
	GSocketConnection* connection;

	g_return_val_if_fail(j_connection_pool != NULL, NULL);
	g_return_val_if_fail(index < j_connection_pool->meta_len, NULL);

	j_trace_enter(G_STRFUNC, NULL);

	connection = j_connection_pool_pop_internal(j_connection_pool->meta_queues[index].queue, &(j_connection_pool->meta_queues[index].count), j_configuration_get_metadata_server(j_connection_pool->configuration, index));

	j_trace_leave(G_STRFUNC);

	return connection;
}

void
j_connection_pool_push_meta (guint index, GSocketConnection* connection)
{
	g_return_if_fail(j_connection_pool != NULL);
	g_return_if_fail(index < j_connection_pool->meta_len);
	g_return_if_fail(connection != NULL);

	j_trace_enter(G_STRFUNC, NULL);

	j_connection_pool_push_internal(j_connection_pool->meta_queues[index].queue, connection);

	j_trace_leave(G_STRFUNC);
}

/**
 * @}
 **/
