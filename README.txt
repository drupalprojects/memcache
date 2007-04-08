## INSTALLATION ##

To install, move memcache.inc to your DRUPAL/includes directory (where the
other *.inc files live).

The memcache.inc file is intended to be used instead of cache.inc, utilizing
Drupal's pluggable cache system. To make this happen, you need to update
$conf in settings.php to tell Drupal which cache_inc file to use:

 $conf = array(
   'memcache_inc' => './includes/memcache.inc',
 );

## SERVERS ##

The available memcached servers are specified in $conf in settings.php. If
you do not specify any servers, memcache.inc assumes that you have a
memcached instance running on localhost:11211. If this is true, and it is
the only memcached instance you wish to use, no further configuration is
required.

If you have more than one memcached instance running, you need to add two
arrays to $conf; memcache_servers and memcache_bins. The arrays follow this
pattern:

'memcache_servers' => array(host1:port => cluster, host2:port => cluster, hostN:port => cluster)

'memcache_bins' => array(bin1 => cluster, bin2 => cluster, binN => cluster)

The bin/cluster/server model can be described as follows:

- Servers are memcached instances identified by host:port.

- Bins are groups of data that get cached together and map 1:1 to the $table
  param in cache_set. Examples from Drupal core are cache_filter,
  cache_menu. The default is 'cache'.

- Clusters are groups of servers that act as a pool.

- many bins can be assigned to a cluster.

- The default cluster is 'default'.

Here is an example configuration that has two clusters, 'default' and
'cluster2'. Five memcached instances are divided up between the two
clusters. 'cache_filter' and 'cache_menu' bins goe to 'cluster2'. All other
bins go to 'default'.

$conf = array(
  'cache_inc' => './includes/memcache.inc',
  'memcache_servers' => array('localhost:11211' => 'default', 
                              'localhost:11212' => 'default', 
                              '123.45.67.890:11211' => 'default', 
                              '123.45.67.891:11211' => 'cluster2', 
                              '123.45.67.892:11211' => 'cluster2'),

  'memcache_bins' => array('cache' => 'default', 
                           'cache_filter' => 'cluster2', 
                           'cache_menu' => 'cluster2'),
);

## PATCHES ##

No patches need to be applied. The patches that are currently part of the
DRUPAL-5--1.dev release should not be applied and will not be part of the
final release. Instead, a new module will be created, advanced_cache, which
will offer these patches as an advanced caching option for sites, with or
without memcache.

## MEMCACHE ADMIN ##

A module offering a UI for memcache is on the way. It will provide stats, a
way to clear the cache, and an interface to organize servers/bins/clusters.
