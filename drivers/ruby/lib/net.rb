require 'socket'
require 'thread'
require 'timeout'

module RethinkDB
  module Faux_Abort
    class Abort
    end
  end

  class RQL
    @@default_conn = nil
    def self.set_default_conn c; @@default_conn = c; end
    def run(c=@@default_conn, opts=nil, &b)
      unbound_if(@body == RQL)
      c, opts = @@default_conn, c if opts.nil? && !c.kind_of?(RethinkDB::Connection)
      opts = {} if opts.nil?
      opts = {opts => true} if opts.class != Hash
      if (tf = opts[:time_format])
        opts[:time_format] = (tf = tf.to_s)
        if tf != 'raw' && tf != 'native'
          raise ArgumentError, "`time_format` must be 'raw' or 'native' (got `#{tf}`)."
        end
      end
      if (gf = opts[:group_format])
        opts[:group_format] = (gf = gf.to_s)
        if gf != 'raw' && gf != 'native'
          raise ArgumentError, "`group_format` must be 'raw' or 'native' (got `#{gf}`)."
        end
      end
      if !c
        raise ArgumentError, "No connection specified!\n" \
        "Use `query.run(conn)` or `conn.repl(); query.run`."
      end
      c.run(@body, opts, &b)
    end
  end

  class Cursor
    include Enumerable
    def out_of_date # :nodoc:
      @conn.conn_id != @conn_id
    end

    def inspect # :nodoc:
      preview_res = @results[0...10]
      if (@results.size > 10 || @more)
        preview_res << (dots = "..."; class << dots; def inspect; "..."; end; end; dots)
      end
      preview = preview_res.pretty_inspect[0...-1]
      state = @run ? "(exhausted)" : "(enumerable)"
      extra = out_of_date ? " (Connection #{@conn.inspect} reset!)" : ""
      "#<RethinkDB::Cursor:#{self.object_id} #{state}#{extra}: #{RPP.pp(@msg)}" +
        (@run ? "" : "\n#{preview}") + ">"
    end

    def initialize(results, msg, connection, opts, token, more = true) # :nodoc:
      @more = more
      @results = results
      @msg = msg
      @run = false
      @conn_id = connection.conn_id
      @conn = connection
      @opts = opts
      @token = token
      fetch_batch
    end

    def each (&block) # :nodoc:
      raise RqlRuntimeError, "Can only iterate over Query_Results once!" if @run
      @run = true
      raise RqlRuntimeError, "Connection has been reset!" if out_of_date
      while true
        @results.each(&block)
        return self if !@more
        res = @conn.wait(@token)
        @results = Shim.response_to_native(res, @msg, @opts)
        if res['t'] == Response::ResponseType::SUCCESS_SEQUENCE
          @more = false
        else
          fetch_batch
        end
      end
    end

    def close
      if @more
        @more = false
        q = [Query::QueryType::STOP]
        res = @conn.run_internal(q, @opts, @token)
        if res['t'] != Response::ResponseType::SUCCESS_SEQUENCE || res.response != []
          raise RqlRuntimeError, "Server sent malformed STOP response #{PP.pp(res, "")}"
        end
        return true
      end
    end

    def fetch_batch
      @conn.set_opts(@token, @opts)
      @conn.dispatch([Query::QueryType::CONTINUE], @token)
    end
  end

  class Connection
    def auto_reconnect(x=true)
      @auto_reconnect = x
      self
    end
    def repl; RQL.set_default_conn self; end

    def initialize(opts={})
      begin
        @abort_module = ::IRB
      rescue NameError => e
        @abort_module = Faux_Abort
      end

      opts = {:host => opts} if opts.class == String
      @host = opts[:host] || "localhost"
      @port = opts[:port] || 28015
      default_db = opts[:db]
      @auth_key = opts[:auth_key] || ""

      @@last = self
      @default_opts = default_db ? {:db => RQL.new.db(default_db)} : {}
      @conn_id = 0
      reconnect(:noreply_wait => false)
    end
    attr_reader :default_db, :conn_id

    @@token_cnt = 0
    def set_opts(token, opts)
      @mutex.synchronize{@opts[token] = opts}
    end
    def run_internal(q, opts, token)
      set_opts(token, opts)
      noreply = opts[:noreply]

      dispatch(q, token)
      noreply ? nil : wait(token)
    end
    def run(msg, opts, &b)
      reconnect(:noreply_wait => false) if @auto_reconnect && (!@socket || !@listener)
      raise RqlRuntimeError, "Error: Connection Closed." if !@socket || !@listener

      global_optargs = {}
      all_opts = @default_opts.merge(opts)
      if all_opts.keys.include?(:noreply)
        all_opts[:noreply] = !!all_opts[:noreply]
      end

      token = (@@token_cnt += 1)
      q = [Query::QueryType::START,
           msg,
           Hash[all_opts.map {|k,v|
                  [k.to_s, (v.class == RQL ? v.to_pb : RQL.new.expr(v).to_pb)]
                }]]

      res = run_internal(q, all_opts, token)
      return res if !res
      if res['t'] == Response::ResponseType::SUCCESS_PARTIAL ||
          res['t'] == Response::ResponseType::SUCCESS_FEED
        value = Cursor.new(Shim.response_to_native(res, msg, opts),
                           msg, self, opts, token, true)
      elsif res['t'] == Response::ResponseType::SUCCESS_SEQUENCE
        value = Cursor.new(Shim.response_to_native(res, msg, opts),
                   msg, self, opts, token, false)
      else
        value = Shim.response_to_native(res, msg, opts)
      end

      if res['p']
        real_val = {
          "profile" => res['p'],
          "value" => value
        }
      else
        real_val = value
      end

      if b
        begin
          b.call(real_val)
        ensure
          value.close if value.class == Cursor
        end
      else
        real_val
      end
    end

    def send packet
      @socket.write(packet)
    end

    def dispatch(msg, token)
      payload = Shim.dump_json(msg).force_encoding('BINARY')
      prefix = [token, payload.bytesize].pack('Q<L<')
      send(prefix + payload)
      return token
    end

    def wait token
      begin
        res = nil
        raise RqlRuntimeError, "Connection closed by server!" if not @listener
        @mutex.synchronize {
          (@waiters[token] = ConditionVariable.new).wait(@mutex) if not @data[token]
          res = @data.delete token if @data[token]
        }
        raise RqlRuntimeError, "Connection closed by server!" if !@listener or !res
        return res
      rescue @abort_module::Abort => e
        print "\nAborting query and reconnecting...\n"
        reconnect(:noreply_wait => false)
        raise e
      end
    end

    # Change the default database of a connection.
    def use(new_default_db)
      @default_opts[:db] = RQL.new.db(new_default_db)
    end

    def inspect
      db = @default_opts[:db] || RQL.new.db('test')
      properties = "(#{@host}:#{@port}) (Default DB: #{db.inspect})"
      state = @listener ? "(listening)" : "(closed)"
      "#<RethinkDB::Connection:#{self.object_id} #{properties} #{state}>"
    end

    @@last = nil
    @@magic_number = VersionDummy::Version::V0_3
    @@wire_protocol = VersionDummy::Protocol::JSON

    def debug_socket; @socket; end

    # Reconnect to the server.  This will interrupt all queries on the
    # server (if :noreply_wait => false) and invalidate all outstanding
    # enumerables on the client.
    def reconnect(opts={})
      raise ArgumentError, "Argument to reconnect must be a hash." if opts.class != Hash
      if not (opts.keys - [:noreply_wait]).empty?
        raise ArgumentError, "reconnect does not understand these options: " +
          (opts.keys - [:noreply_wait]).to_s
      end
      opts[:noreply_wait] = true if not opts.keys.include?(:noreply_wait)

      self.noreply_wait() if opts[:noreply_wait]

      stop_listener
      @socket.close rescue nil if @socket
      @socket = TCPSocket.open(@host, @port)
      @waiters = {}
      @opts = {}
      @data = {}
      @mutex = Mutex.new
      @conn_id += 1
      start_listener

      self
    end

    def close(opts={})
      raise ArgumentError, "Argument to close must be a hash." if opts.class != Hash
      if not (opts.keys - [:noreply_wait]).empty?
        raise ArgumentError, "close does not understand these options: " +
          (opts.keys - [:noreply_wait]).to_s
      end
      opts[:noreply_wait] = true if not opts.keys.include?(:noreply_wait)

      self.noreply_wait() if opts[:noreply_wait]
      @listener.terminate if @listener
      @listener = nil
      @socket.close
      @socket = nil
    end

    def noreply_wait
      raise RqlRuntimeError, "Error: Connection Closed." if !@socket || !@listener
      q = [Query::QueryType::NOREPLY_WAIT]
      res = run_internal(q, {noreply: false}, @@token_cnt += 1)
      if res['t'] != Response::ResponseType::WAIT_COMPLETE
        raise RqlRuntimeError, "Unexpected response to noreply_wait: " + PP.pp(res, "")
      end
      nil
    end

    def self.last
      return @@last if @@last
      raise RqlRuntimeError, "No last connection.  Use RethinkDB::Connection.new."
    end

    def stop_listener
      if @listener
        @listener.terminate
        @listener.join
        @listener = nil
      end
    end

    def note_data(token, data) # Synchronize around this!
      @data[token] = data
      @opts.delete(token)
      @waiters.delete(token).signal if @waiters[token]
    end

    def note_error(token, e) # Synchronize around this!
      data = {
        't' => 16,
        'r' => [e.inspect],
        'b' => []
      }
      note_data(token, data)
    end

    def start_listener
      class << @socket
        def maybe_timeout(sec=nil, &b)
          sec ? timeout(sec, &b) : b.call
        end
        def read_exn(len, timeout_sec=nil)
          maybe_timeout(timeout_sec) {
            buf = read len
            if !buf or buf.length != len
              raise RqlRuntimeError, "Connection closed by server."
            end
            return buf
          }
        end
      end
      @socket.write([@@magic_number, @auth_key.size].pack('L<L<') +
                    @auth_key + [@@wire_protocol].pack('L<'))
      response = ""
      while response[-1..-1] != "\0"
        response += @socket.read_exn(1, 20)
      end
      response = response[0...-1]
      if response != "SUCCESS"
        raise RqlRuntimeError,"Server dropped connection with message: \"#{response}\""
      end

      stop_listener if @listener
      @listener = Thread.new {
        while true
          begin
            token = -1
            token = @socket.read_exn(8).unpack('q<')[0]
            response_length = @socket.read_exn(4).unpack('L<')[0]
            response = @socket.read_exn(response_length)
            begin
              data = Shim.load_json(response, @opts[token])
            rescue Exception => e
              raise RqlRuntimeError, "Bad response, server is buggy.\n" +
                "#{e.inspect}\n" + response
            end
            if token == -1
              @mutex.synchronize{@waiters.keys.each{|k| note_data(k, data)}}
            else
              @mutex.synchronize{note_data(token, data)}
            end
          rescue Exception => e
            if token == -1
              @mutex.synchronize {
                @listener = nil
                @waiters.keys.each{|k| note_error(k, e)}
              }
              Thread.current.terminate
              abort("unreachable")
            else
              @mutex.synchronize{note_error(token, e)}
            end
          end
        end
      }
    end
  end
end
