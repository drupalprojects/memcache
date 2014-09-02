<?php

/**
 * @file
 * Contains \Drupal\memcache\DrupalMemcacheBase.
 */

namespace Drupal\memcache;

use Drupal\Core\Site\Settings;
use Psr\Log\LogLevel;

/**
 * Class DrupalMemcacheBase.
 */
abstract class DrupalMemcacheBase implements DrupalMemcacheInterface {

  /**
   * The cache bin name.
   *
   * @var string
   */
  protected $bin;

  /**
   * The settings object.
   *
   * @var \Drupal\Core\Site\Settings
   */
  protected $settings;

  /**
   * The memcache object.
   *
   * @var mixed
   *   E.g. \Memcache|\Memcached
   */
  protected $memcache;

  /**
   * The hash algorithm to pass to hash(). Defaults to 'sha1'
   *
   * @var string
   */
  protected $hashAlgorithm;

  /**
   * Constructs a DrupalMemcacheBase object.
   *
   * @param string $bin
   *   The cache bin.
   * @param \Drupal\Core\Site\Settings
   *   The settings object.
   */
  public function __construct($bin, Settings $settings) {
    $this->bin = $bin;
    $this->settings = $settings;

    $this->hashAlgorithm = $this->settings->get('memcache_key_hash_algorithm', 'sha1');
  }

  /**
   * {@inheritdoc}
   */
  public function get($key) {
    $full_key = $this->key($key);

    $track_errors = ini_set('track_errors', '1');
    $php_errormsg = '';
    $result = @$this->memcache->get($full_key);

    if (!empty($php_errormsg)) {
      register_shutdown_function('memcache_log_warning', LogLevel::WARNING, 'Exception caught in DrupalMemcacheBase::get: !msg', array('!msg' => $php_errormsg));
      $php_errormsg = '';
    }
    ini_set('track_errors', $track_errors);

    return $result;
  }

  /**
   * {@inheritdoc}
   */
  public function key($key) {
    $full_key = urlencode($this->bin . '-' . $key);

    // Memcache only supports key lengths up to 250 bytes.  If we have generated
    // a longer key, we shrink it to an acceptable length with a configurable
    // hashing algorithm. Sha1 was selected as the default as it performs
    // quickly with minimal collisions.
    if (strlen($full_key) > 250) {
      $full_key = urlencode(hash($this->hashAlgorithm, $this->bin . '-' . $key));
    }

    return $full_key;
  }

  /**
   * {@inheritdoc}
   */
  public function delete($key) {
    $full_key = $this->key($key);
    return $this->memcache->delete($full_key, 0);
  }

  /**
   * {@inheritdoc}
   */
  public function flush() {
    $this->memcache->flush();
  }

}
