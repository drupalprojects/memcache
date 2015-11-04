
## Requirements ##

- PHP 5.1 or greater
- Availability of a memcached daemon: http://memcached.org/
- One of the two PECL memcache packages:
  - http://pecl.php.net/package/memcache (older, most stable)
  - http://pecl.php.net/package/memcached (newer, possible performance issues)

## INSTALLATION ##

These are the broad steps you need to take in order to use this software. Order
is important.

1. Install the memcached binaries on your server. See 

http://www.lullabot.com/articles/how_install_memcache_debian_etch

2. Install the PECL memcache extension for PHP. This must be version 2.2.1 or 
   higher or you will experience errors.
3. Put your site into offline mode.
4. Download and install the memcache module.
5. If you have previously been running the memcache module, run update.php.
6. Start at least one instance of memcached on your server.
7. Edit settings.php to configure the servers, clusters and bins that memcache
   is supposed to use.
8. Edit settings.php to include either memcache.inc. For
   example, $conf['cache_inc'] ='sites/all/modules/memcache/memcache.inc';
9. Bring your site back online.

For instructions on 1 and 2 above, please see the INSTALLATION.txt file that
comes with the memcache module download.

The memcache.inc file is intended to be used instead of cache.inc, utilizing
Drupal's pluggable cache system.

memcache.db.inc IS DEPRECATED AND IS NOT RECOMMENDED. It is still distributed
with the 6.x-1.x branch, but will not be included in any further versions and
may be removed in future 6.x releases.

Update $conf in settings.php to tell Drupal where the cache_inc file is:

 $conf = array(
   // The path to wherever memcache.inc is. The easiest is to simply point it
   // to the copy in your module's directory.
   'cache_inc' => './sites/all/modules/memcache/memcache.inc',
 );

## SERVERS ##

If you want the simple version, you can start one default memcache instance on
your web server like this: memcached -m 24 -p 11211 -d
If that is enough to meet your needs, there is no more configuration needed. If
you want to utilize this module's sophisticated clustering feature and spread
your cache over several machines, or if your cache is found on a machine other
than your web server, read on.

The available memcached servers are specified in $conf in settings.php. If
you do not specify any servers, memcache.inc assumes that you have a
memcached instance running on 10.0.0.1:11211. If this is true, and it is
the only memcached instance you wish to use, no further configuration is
required.

If you have more than one memcached instance running, you need to add two
arrays to $conf; memcache_servers and memcache_bins. The arrays follow this
pattern:

'memcache_servers' => array(
  host1:port => cluster, 
  host2:port => cluster, 
  hostN:port => cluster
)

'memcache_bins' => array(bin1 => cluster, bin2 => cluster, binN => cluster)

The bin/cluster/server model can be described as follows:

- Servers are memcached instances identified by host:port.

- Bins are groups of data that get cached together and map 1:1 to the $table
  param in cache_set(). Examples from Drupal core are cache_filter,
  cache_menu. The default is 'cache'.

- Clusters are groups of servers that act as a memory pool.

- many bins can be assigned to a cluster.

- The default cluster is 'default'.

Here is a simple setup that has two memcached instances, both running on
localhost. The 11212 instance belongs to the 'pages' cluster and the table
cache_page is mapped to the 'pages' cluster. Thus everything that gets cached,
with the exception of the page cache (cache_page), will be put into 'default',
or the 11211 instance. The page cache will be in 11212.

$conf = array(
  ...
  // Important to define a default cluster in both the servers
  // and in the bins. This links them together.
  'memcache_servers' => array('10.0.0.1:11211' => 'default',
                              '10.0.0.1:11212' => 'pages'),
  'memcache_bins' => array('cache' => 'default',
                           'cache_page' => 'pages'),
);

Here is an example configuration that has two clusters, 'default' and
'cluster2'. Five memcached instances are divided up between the two
clusters. 'cache_filter' and 'cache_menu' bins go to 'cluster2'. All other
bins go to 'default'.

$conf = array(
  'cache_inc' => './sites/all/modules/memcache/memcache.inc',
  'memcache_servers' => array('10.0.0.1:11211' => 'default',
                              '10.0.0.1:11212' => 'default',
                              '123.45.67.890:11211' => 'default',
                              '123.45.67.891:11211' => 'cluster2',
                              '123.45.67.892:11211' => 'cluster2'),

  'memcache_bins' => array('cache' => 'default',
                           'cache_filter' => 'cluster2',
                           'cache_menu' => 'cluster2'),
);

Here is an example configuration where the 'cache_form' bin is set to bypass
memcache and use the standard table-based Drupal cache by assigning it to a
cluster called 'database'.

$conf = array(
  ...
  'memcache_servers' => array('10.0.0.1:11211' => 'default'),
  'memcache_bins' => array('cache' => 'default',
                           'cache_form' => 'database'),
);

## memcache_extra_include and database.inc ##

In the above example, mapping a bin to 'database' makes a cache be stored
in the database instead of memcache. This is actually done by the file
database.inc, which is copy and pasted from DRUPAL/includes/cache.inc. 
If you want to provide an alternate file instead of database.inc to handle
the cache calls to 'database', override the variable memcache_extra_include
in settings.php to provide the location of the file to include. This only
applies if you are using memcache.inc (not memcache.db.inc, which is deprecated).


## PREFIXING ##

If you want to have multiple Drupal installations share memcached instances,
you need to include a unique prefix for each Drupal installation in the $conf
array of settings.php:

$conf = array(
  ...
  'memcache_key_prefix' => 'something_unique',
);

## MAXIMUM LENGTHS ##

If the length of your prefix + key + bin combine to be more than 250 characters,
they will be automatically hashed. Memcache only supports key lengths up to 250
bytes. You can optionally configure the hashing algorithm used, however sha1 was
selected as the default because it performs quickly with minimal collisions.

Visit http://www.php.net/manual/en/function.hash-algos.php to learn more about
which hash algorithms are available.

$conf['memcache_key_hash_algorithm'] = 'sha1';

You can also tune the maximum key length BUT BE AWARE this doesn't affect
memcached's server-side limitations -- this value is primarily exposed to allow
you to further shrink the length of keys to optimize network performance.
Specifying a length larger than 250 will almost certainly lead to problems
unless you know what you're doing.

$conf['memcache_key_max_length'] = 250;

By default, the memcached server can store objects up to 1 MiB in size. It's
possible to increase the memcached page size to support larger objects, but this
can also lead to wasted memory. Alternatively, the Drupal memcache module splits
these large objects into smaller pieces. By default, the Drupal memcache module
splits objects into 1 MiB sized pieces. You can modify this with the following
tunable to match any special server configuration you may have. NOTE: Increasing
this value without making changes to your memcached server can result in
failures to cache large items.

(Note: 1 MiB = 1024 x 1024 = 1048576.)

$conf['memcache_data_max_length'] = 1048576;

It is generally undesirable to store excessively large objects in memcache as
this can result in a performance penalty. Because of this, by default the Drupal
memcache module logs any time an object is cached that has to be split into
multiple pieces. If this is generating too many watchdog logs, you should first
understand why these objects are so large and if anything can be done to make
them smaller. If you determine that the large size is valid and is not causing
you any unnecessary performance penalty, you can tune the following variable to
minimize or disable this logging. Set the value to a positive integer to only
log when an object is split into this many or more pieces. For example, if
memcache_data_max_length is set to 1048576 and memcache_log_data_pieces is set
to 5, watchdog logs will only be written when an object is split into 5 or more
pieces (objects >4 MiB in size). Or, to to completely disable logging set
memcache_log_data_pieces to 0 or FALSE.

$conf['memcache_log_data_pieces'] = 2;

## SESSIONS ##

Here is a sample config that uses memcache for sessions. Note you MUST have
a session and a users server set up for memcached sessions to work.

$conf = array(
  'cache_inc' => './sites/all/modules/memcache/memcache.inc',
  'session_inc' => './sites/all/modules/memcache/memcache-session.inc',
  'memcache_servers' => array(
    'localhost:11211' => 'default',
    'localhost:11212' => 'filter',
    'localhost:11213' => 'menu',
    'localhost:11214' => 'page',
    'localhost:11215' => 'session',
    'localhost:11216' => 'users',
  ),
  'memcache_bins' => array(
    'cache' => 'default',
    'cache_filter' => 'filter',
    'cache_menu' => 'menu',
    'cache_page' => 'page',
    'session' => 'session',
    'users' => 'users',
  ),
);


## TROUBLESHOOTING ##

PROBLEM:
 Error:
  Failed to load required file memcache/dmemcache.inc

SOLUTION:
You need to enable memcache in settings.php. Search for "Example 1" above
for a basic configuration example.

PROBLEM:
 Error:
  PECL !extension version %version is unsupported. Please update to
  %recommended or newer.

SOLUTION:
Upgrade to the latest available PECL extension release. Older PECL extensions
have known bugs and cause a variety of problems when using the memcache module.

PROBLEM:
 Error:
  Failed to connect to memcached server instance at <IP ADDRESS>.

SOLUTION:
Verify that the memcached daemon is running at the specified IP and PORT. To
debug you can try to telnet directly to the memcache server from your web
servers, example:
   telnet localhost 11211

PROBLEM:
 Error:
  Failed to store to then retrieve data from memcache.

SOLUTION:
Carefully review your settings.php configuration against the above
documentation. This error simply does a cache_set followed by a cache_get
and confirms that what is written to the cache can then be read back again.
This test was added in the 7.x-1.1 release.

The following code is what performs this test -- you can wrap this in a <?php
tag and execute as a script with 'drush scr' to perform further debugging.

        $cid = 'memcache_requirements_test';
        $value = 'OK';
        // Temporarily store a test value in memcache.
        cache_set($cid, $value);
        // Retreive the test value from memcache.
        $data = cache_get($cid);
        if (!isset($data->data) || $data->data !== $value) {
          echo t('Failed to store to then retrieve data from memcache.');
        }
        else {
          // Test a delete as well.
          cache_clear_all($cid, 'cache');
        }

PROBLEM:
 Error:
  Unexpected failure when testing memcache configuration.

SOLUTION:
Be sure the memcache module is properly installed, and that your settings.php
configuration is correct. This error means an exception was thrown when
attempting to write to and then read from memcache.

PROBLEM:
 Error:
  Failed to set key: Failed to set key: cache_page-......

SOLUTION:
Upgrade your PECL library to PECL package (2.2.1) (or higher).

WARNING:
Zlib compression at the php.ini level and Memcache conflict.
See http://drupal.org/node/273824

## MEMCACHE ADMIN ##

A module offering a UI for memcache is included. It provides aggregated and
per-page statistics for memcache.


## Memcached PECL Extension Support

The Drupal memcache module supports both the memcache and the memcached PECL
extensions.  If both extensions are installed the older memcache extension will
be used by default.  If you'd like to use the newer memcached extension remove
the memcache extension from your system or configure settings.php to force
your website to use the newer extension:
  $conf['memcache_extension'] = 'memcached';

The newer memcached PECL extension uses libmemcached on the backend and allows
you to use some of the newer advanced features in memcached 1.4.  It is highly
recommended that you test with both PECL extensions to determine which is a
better fit for your infrastructure.  CAUTION: There have been performance and
functionality regressions reported when using the memcached extension.

NOTE: It is important to realize that the memcache php.ini options do not impact
the memcached extension, this new extension doesn't read in options that way.
Instead, it takes options directly from Drupal. Because of this, you must
configure memcached in settings.php. Please look here for possible options:

http://us2.php.net/manual/en/memcached.constants.php

An example configuration block is below, this block also illustrates our
default options. These will be set unless overridden in settings.php.

$conf['memcache_options'] = array(
  Memcached::OPT_COMPRESSION => FALSE,
  Memcached::OPT_DISTRIBUTION => Memcached::DISTRIBUTION_CONSISTENT,
);

These are as follows:

 * Turn off compression, as this takes more CPU cycles than its worth for most
   users
 * Turn on consistent distribution, which allows you to add/remove servers
   easily

If you are using memcached 1.4 or above, you should enable the binary protocol,
which is more advanced and faster, by adding the following to settings.php:

$conf['memcache_options'] = array(
  Memcached::OPT_BINARY_PROTOCOL => TRUE,
);


## Stampede protection ##

Memcache now includes stampede protection for expired and invalid cache items.
To enable stampede protection, enable it in settings.php
  $conf['memcache_stampede_protection'] = TRUE;

To avoid lock stampedes, it is important that you enable the memcache lock
implementation when enabling stampede protection -- enabling stampede protection
without enabling the Memcache lock implementation can cause worse performance and
can result in dropped locks due to key-length truncation.
  $conf['lock_inc'] = './sites/all/modules/memcache/memcache-lock.inc';

Memcache stampede protection is primarily designed to benefit the following
caching pattern: a miss on a cache_get() for a specific cid is immediately
followed by a cache_set() for that cid. Of course, this is not the only caching
pattern used in Drupal, so stampede protection can be selectively disabled for
optimal performance.  For example, a cache miss in Drupal core's
module_implements() won't execute a cache_set until drupal_page_footer()
calls module_implements_write_cache() which can occur much later in page
generation.  To avoid long hanging locks, stampede protection should be
disabled for these delayed caching patterns.

Memcache stampede protection can be disabled for entire bins, specific cid's in
specific bins, or cid's starting with a specific prefix in specific bins. For
example:

  $conf['memcache_stampede_protection_ignore'] = array(
    // Ignore the variables cache and all cids starting with 'i18n:string:'
    // in the cache bin.
    'cache' => array(
      'variables',
      'i18n:string:*',
    ),
    // Disable stampede protection for the entire 'cache_path' and 'cache_rules'
    // bins.
    'cache_path',
    'cache_rules',
  );

Only change the following stampede protection tunables if you're sure you know
what you're doing, which requires first reading the memcache.inc code.

The value passed to lock_acquire. Defaults to '15'.
  $conf['memcache_stampede_semaphore'] = 15;

The value to pass to lock_wait, defaults to 5.
  $conf['memcache_stampede_wait_time'] = 5;

The limit of calls to lock_wait() due to stampede protection during one request.
Defaults to 3.
  $conf['memcache_stampede_wait_limit'] = 3;

When setting these variables, note that:
 - there is unlikely to be a good use case for setting wait_time higher
   than stampede_semaphore.
 - wait_time * wait_limit is designed to default to a number less than
   standard web server timeouts (i.e. 15 seconds vs. apache's default of
   30 seconds).

## Persistent connections ##

If you are using the Memcache PECL extension you can specify whether or not to
connect using persistent connections in settings.php. If you do not specify a
value it defaults to FALSE.  For example, to enable persistent connections
add the following to your settings.php file:
$conf['memcache_persistent'] = TRUE;

Persistent connections when using the Memcached PECL extension are currently
not supported.  See http://drupal.org/node/822316#comment-4427676 for further
details.
