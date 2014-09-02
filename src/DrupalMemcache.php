<?php

/**
 * @file
 * Contains \Drupal\memcache\DrupalMemcache.
 */

namespace Drupal\memcache;

use Drupal\Core\Site\Settings;
use Psr\Log\LogLevel;

/**
 * Class DrupalMemcache.
 */
class DrupalMemcache extends DrupalMemcacheBase {

  /**
   * {@inheritdoc}
   */
  public function __construct($bin, Settings $settings) {
    parent::__construct($bin, $settings);

    $this->memcache = new \Memcache();
  }

  /**
   * @{@inheritdoc}
   */
  public function addServer($server_path, $persistent = FALSE) {
    list($host, $port) = explode(':', $server_path);

    // Support unix sockets in the format 'unix:///path/to/socket'.
    if ($host == 'unix') {
      // When using unix sockets with Memcache use the full path for $host.
      $host = $server_path;
      // Port is always 0 for unix sockets.
      $port = 0;
    }

    // When using the PECL memcache extension, we must use ->(p)connect
    // for the first connection.
    return $this->connect($host, $port, $persistent);
  }

  /**
   * Connects to a memcache server.
   *
   * @param string $host
   * @param int $port
   * @param bool $persistent
   *
   * @return bool|mixed
   */
  protected function connect($host, $port, $persistent) {
    if ($persistent) {
      return @$this->memcache->pconnect($host, $port);
    }
    else {
      return @$this->memcache->connect($host, $port);
    }
  }

  /**
   * {@inheritdoc}
   */
  public function set($key, $value, $exp = 0, $flag = FALSE) {
    $full_key = $this->key($key);
    return $this->memcache->set($full_key, $value, $flag, $exp);
  }

  /**
   * {@inheritdoc}
   */
  public function getMulti(array $keys) {
    $full_keys = array();

    foreach ($keys as $cid) {
      $full_key = $this->key($cid);
      $full_keys[$cid] = $full_key;
    }

    $track_errors = ini_set('track_errors', 1);
    $php_errormsg = '';
    $results = @$this->memcache->get($full_keys);

    if (!empty($php_errormsg)) {
      register_shutdown_function('memcache_log_warning', LogLevel::WARNING, 'Exception caught in DrupalMemcache::getMulti: !msg', array('!msg' => $php_errormsg));
      $php_errormsg = '';
    }

    ini_set('track_errors', $track_errors);

    // If $results is FALSE, convert it to an empty array.
    if (!$results) {
      $results = array();
    }

    // Convert the full keys back to the cid.
    $cid_results = array();
    $cid_lookup = array_flip($full_keys);
    foreach ($results as $key => $value) {
      $cid_results[$cid_lookup[$key]] = $value;
    }

    return $cid_results;
  }

}
