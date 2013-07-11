<?php

/**
 * @file
 * Contains \Drupal\memcache\MemcacheBackendFactory.
 */

namespace Drupal\memcache;

use Drupal\Core\Config\ConfigFactory;
use Drupal\Core\Lock\LockBackendInterface;

/**
 * Class DatabaseBackendFactory
 * @package Drupal\memcache
 */
class MemcacheBackendFactory {

  /**
   * The lock backend that should be used.
   *
   * @var \Drupal\Core\Lock\LockBackendInterface
   */
  protected $lock;

  /**
   * The config factory instance.
   *
   * @var \Drupal\Core\Config\ConfigFactory
   */
  protected $configFactory;

  /**
   * Constructs the DatabaseBackendFactory object.
   *
   * @param \Drupal\Core\Lock\LockBackendInterface $lock
   * @param \Drupal\Core\Config\ConfigFactory $config_factory
   */
  function __construct(LockBackendInterface $lock, ConfigFactory $config_factory) {
    $this->lock = $lock;
    $this->configFactory = $config_factory;
  }

  /**
   * Gets MemcacheBackend for the specified cache bin.
   *
   * @param $bin
   *   The cache bin for which the object is created.
   *
   * @return \Drupal\memcache\MemcacheBackend
   *   The cache backend object for the specified cache bin.
   */
  function get($bin) {
    return new MemcacheBackend($this->lock, $this->configFactory, $bin);
  }

}
