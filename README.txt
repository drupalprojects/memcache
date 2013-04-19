## IMPORTANT NOTE ##

This file contains installation instructions for the 7.x-1.x version of the
Drupal Memcache module. Configuration differs between 7.x and 6.x versions
of the module, so be sure to follow the 6.x instructions if you are configuring
the 6.x-1.x version of this module!

## REQUIREMENTS ##

- PHP 5.1 or greater
- Availability of a memcached daemon: http://memcached.org/
- One of the two PECL memcache packages:
  - http://pecl.php.net/package/memcache (recommended)
  - http://pecl.php.net/package/memcached (latest versions require PHP 5.2 or
    greater)

## INSTALLATION ##

These are the steps you need to take in order to use this software. Order
is important.

 1. Install the memcached binaries on your server and start the memcached
    service.
 2. Install your chosen PECL memcache extension -- this is the memcache client
    library which will be used by the Drupal memcache module to interact with
    the memcached server(s). Generally PECL memcache (3.0.6+) is recommended,
    but PECL memcached (2.0.1+) also works well for some people. Use of older
    versions may cause problems.
 3. Put your site into offline mode.
 4. Download and install the memcache module.
 5. If you have previously been running the memcache module, run update.php.
 6. Edit settings.php to configure the servers, clusters and bins that memcache
    is supposed to use.
 7. Edit settings.php to make memcache the default cache class, for example:
      $conf['cache_backends'][] = 'sites/all/modules/memcache/memcache.inc';
      $conf['cache_default_class'] = 'MemCacheDrupal';
    The cache_backends path needs to be adjusted based on where you installed
    the module.
 8. Make sure the following line also exists, to ensure that the special
    cache_form bin is assigned to non-volatile storage:
      $conf['cache_class_cache_form'] = 'DrupalDatabaseCache';
 9. Bring your site back online.

For more detailed instructions on (1) and (2) above, please see the
documentation online on drupal.org which includes links to external
walk-throughs for various operating systems.

## Advanced Configuration ##

This module is capable of working with one memcached instance or with multiple
memcached instances run across one or more servers. The default is to use one
server accessible on localhost port 11211. If that meets your needs, then the
configuration settings outlined above are sufficient for the module to work.
If you want to use multiple memcached instances, or if you are connecting to a
memcached instance located on a remote machine, further configuration is
required.

The available memcached servers are specified in $conf in settings.php. If you
do not specify any servers, memcache.inc assumes that you have a memcached
instance running on localhost:11211. If this is true, and it is the only
memcached instance you wish to use, no further configuration is required.

If you have more than one memcached instance running, you need to add two arrays
to $conf; memcache_servers and memcache_bins. The arrays follow this pattern:

'memcache_servers' => array(
  server1:port => cluster1,
  server2:port => cluster2,
  serverN:port => clusterN,
  'unix:///path/to/socket' => clusterS
)

'memcache_bins' => array(
   bin1 => cluster1,
   bin2 => cluster2,
   binN => clusterN,
   binS => clusterS
)

The bin/cluster/server model can be described as follows:

- Servers are memcached instances identified by host:port.

- Clusters are groups of servers that act as a memory pool. Each cluster can
  contain one or more servers.

- Bins are groups of data that get cached together and map 1:1 to the $table
  parameter of cache_set(). Examples from Drupal core are cache_filter and
  cache_menu. The default is 'cache'.

- Multiple bins can be assigned to a cluster.

- The default cluster is 'default'.

## EXAMPLES ##

Example 1:

First, the most basic configuration which consists of one memcached instance
running on localhost port 11211 and all caches except for cache_form being
stored in memcache. This requires minimal configuration as it is the default
behavior:

  $conf['cache_backends'][] = 'sites/all/modules/memcache/memcache.inc';
  $conf['cache_default_class'] = 'MemCacheDrupal';
  // The 'cache_form' bin must be assigned to non-volatile storage.
  $conf['cache_class_cache_form'] = 'DrupalDatabaseCache';

Note that no servers or bins are defined.  The default server and bin
configuration which is used in this case is equivalant to setting:

  $conf['memcache_servers'] = array('localhost:11211' => 'default');


Example 2:

In this example we define three memcached instances, two accessed over the
network on localhost, and one on a Unix socket -- please note this is not a
recommended configuration and it's highly unlikely you'd want to configure
memcache to use both sockets and network addresses like this, instead you'd
consistently use one or the other.

The instance on localhost port 11211 belongs to the 'default' cluster where
everything gets cached that isn't otherwise defined. (We refer to it as a
"cluster", but in our example our "clusters" involve only one instance.) The
instance on port 11212 belongs to the 'pages' cluster, with the 'cache_page'
table mapped to it -- so the Drupal page cache is stored in this cluster.
Finally, the instance listening on a socket is part of the 'blocks' cluster,
with the 'cache_block' table mapped to it -- so the Drupal block cache is
stored here. Note that sockets do not have ports.

  $conf['cache_backends'][] = 'sites/all/modules/memcache/memcache.inc';
  $conf['cache_default_class'] = 'MemCacheDrupal';

  // The 'cache_form' bin must be assigned no non-volatile storage.
  $conf['cache_class_cache_form'] = 'DrupalDatabaseCache';

  // Important to define a default cluster in both the servers
  // and in the bins. This links them together.
  $conf['memcache_servers'] = array('localhost:11211' => 'default',
                                    'localhost:11212' => 'pages',
                                    'unix:///path/to/socket' => 'blocks');
  $conf['memcache_bins'] = array('cache' => 'default',
                                 'cache_page' => 'pages',
                                 'cache_block' => 'blocks');


Example 3:

Here is an example configuration that has two clusters, 'default' and
'cluster2'. Five memcached instances are divided up between the two
clusters. 'cache_filter' and 'cache_menu' bins go to 'cluster2'. All other
bins go to 'default'.

  $conf['cache_backends'][] = 'sites/all/modules/memcache/memcache.inc';
  $conf['cache_default_class'] = 'MemCacheDrupal';
  // The 'cache_form' bin must be assigned no non-volatile storage.
  $conf['cache_class_cache_form'] = 'DrupalDatabaseCache';

  $conf['memcache_servers'] = array('123.45.67.89:11211' => 'default',
                                    '123.45.67.89:11212' => 'default',
                                    '123.45.67.90:11211' => 'default',
                                    '123.45.67.91:11211' => 'cluster2',
                                    '123.45.67.92:11211' => 'cluster2');

  $conf['memcache_bins'] = array('cache' => 'default',
                                 'cache_filter' => 'cluster2',
                                 'cache_menu' => 'cluster2');
  );


## PREFIXING ##

If you want to have multiple Drupal installations share memcached instances,
you need to include a unique prefix for each Drupal installation in the $conf
array of settings.php:

$conf['memcache_key_prefix'] = 'something_unique';

## MULTIPLE SERVERS ##

To use this module with multiple memcached servers, it is important that you set
the hash strategy to consistent. This is controlled in the PHP extension, not the
Drupal module.

If using PECL memcache:
Edit /etc/php.d/memcache.ini (path may changed based on package/distribution) and
set the following:
memcache.hash_strategy=consistent

You need to reload apache httpd after making that change.

If using PECL memcached:
Memcached options can be controlled in settings.php.  The following setting is
needed:
$conf['memcache_options'] = array(
  Memcached::OPT_DISTRIBUTION => Memcached::DISTRIBUTION_CONSISTENT,
);

## SESSIONS ##

NOTE: Session.inc is not yet ported to Drupal 7 and is not recommended for use
in production..

Here is a sample config that uses memcache for sessions. Note you MUST have
a session and a users server set up for memcached sessions to work.

$conf['cache_backends'][] = 'sites/all/modules/memcache/memcache.inc';
$conf['cache_default_class'] = 'MemCacheDrupal';

// The 'cache_form' bin must be assigned no non-volatile storage.
$conf['cache_class_cache_form'] = 'DrupalDatabaseCache';
$conf['session_inc'] = './sites/all/modules/memcache/memcache-session.inc';

$conf['memcache_servers'] = array(
    'localhost:11211' => 'default',
    'localhost:11212' => 'filter',
    'localhost:11213' => 'menu',
    'localhost:11214' => 'page',
    'localhost:11215' => 'session',
    'localhost:11216' => 'users',
);
$conf['memcache_bins'] = array(
    'cache' => 'default',
    'cache_filter' => 'filter',
    'cache_menu' => 'menu',
    'cache_page' => 'page',
    'session' => 'session',
    'users' => 'users',
);


## TROUBLESHOOTING ##

PROBLEM:
Error:
Failed to set key: Failed to set key: cache_page-......

SOLUTION:
Upgrade your PECL library to PECL package (2.2.1) (or higher).

WARNING:
Zlib compression at the php.ini level and Memcache conflict.
See http://drupal.org/node/273824

## MEMCACHE ADMIN ##

A module offering a UI for memcache is included. It provides stats, a
way to clear the cache, and an interface to organize servers/bins/clusters.

## Memcached PECL Extension Support

We also support the Memcached PECL extension. This extension backends
to libmemcached and allows you to use some of the newer advanced features in
memcached 1.4.

NOTE: It is important to realize that the memcache php.ini options do not impact
the memcached extension, this new extension doesn't read in options that way.
Instead, it takes options directly from Drupal. Because of this, you must
configure memcached in settings.php. Please look here for possible options:

http://us2.php.net/manual/en/memcached.constants.php

An example configuration block is below, this block also illustrates our
default options (selected through performance testing). These options will be
set unless overridden in settings.php.

$conf['memcache_options'] = array(
  Memcached::OPT_COMPRESSION => FALSE,
  Memcached::OPT_DISTRIBUTION => Memcached::DISTRIBUTION_CONSISTENT,
);

These are as follows:

 * Turn off compression, as this takes more CPU cycles than it's worth for most
   users
 * Turn on consistent distribution, which allows you to add/remove servers
   easily

Other options you could experiment with:
 + Memcached::OPT_BINARY_PROTOCOL => TRUE,
    * This enables the Memcache binary protocol (only available in Memcached
      1.4 and later). Note that some users have reported SLOWER performance
      with this feature enabled. It should only be enabled on extremely high
      traffic networks where memcache network traffic is a bottleneck.
      Additional reading about the binary protocol:
        http://code.google.com/p/memcached/wiki/MemcacheBinaryProtocol

 + Memcached::OPT_TCP_NODELAY => TRUE,
    * This enables the no-delay feature for connecting sockets; it's been
      reported that this can speed up the Binary protocol (see above). This
      tells the TCP stack to send packets immediately and without waiting for
      a full payload, reducing per-packet network latency (disabling "Nagling").
