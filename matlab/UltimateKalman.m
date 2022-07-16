classdef UltimateKalman < handle
    % UltimateKalman   An implementation of the Paige-Saunders Kalman
    % filter and smoother by Sivan Toledo, Tel Aviv University.
    %
    % The filter is advanced by calling evolve and then observe every
    % step.
    %
    % To predict the next state(s) before providing the observations
    % (possibly before you have them) call observe and then filtered. Then
    % you can roll back and provide observations.
    %
    % UltimateKalman Methods:
    %    evolve   - Evolve the state using a linear matrix equation
    %    observe  - Provide observations of the current state
    %    filtered - Obtain an estimate of the current state
    %    forget   - Forget the oldest steps to save memory meory
    %    smooth   - Compute smooth estimates of all the stored states
    %    smoothed - Obtain the smoothed estimates of historical states

    properties (Access = private)
        steps;   % old states
        current; % the current state; moved to steps at the end of observe

        k;     % old, now sure what this was
    end

    methods (Access = public)
        function kalman = UltimateKalman()
            kalman = kalman@handle();
            kalman.steps = {};
        end

        function i = earliest(kalman)
            if isempty(kalman.steps)
                i = -1;
            else
                i = kalman.steps{ 1 }.step;
            end
        end

        function i = latest(kalman)
            if isempty(kalman.steps)
                i = -1;
            else
                i = kalman.steps{ length(kalman.steps) }.step;
            end
        end

        function evolve(kalman,n_i,H_i,F_i,c_i,K_i)
            %EVOLVE   Evolve the state using the given linear recurrence.
            %   kalman.EVOLVE(H_i,F_i,c_i,K_i) evolves the state using the recurrence
            %                H_i * u_i = F_i * u_{i-1} + c_i + epsilon
            %   where C_i is the covariance matrix of the error term epsilon.
            %   The covariance matrix C_i must be an instance of
            %   CovarianceMatrix. The matrices H and F are standard Matlab
            %   matrices, and be is a standard column vector.
            % 
            %   In the first step, this method does nothing; the first state is
            %   not an evolution of an earlier state. You can omit
            %   arguments or provide empty arguments in the first step.
            %
            %   The argument H can be empty, []. If it is, H is taken to be
            %   an identity matrix (possibly rectangular, if the dimension of
            %   this state is larger than the dimension of the previous state).

            kalman.current = []; % clear
            kalman.current.dimension = n_i;
            if isempty(kalman.steps) % starting the first step
                kalman.current.step = 0;
                %kalman.current.local = 1; % local index in steps
                return
            end
                
            ptr_imo = length(kalman.steps);                                % pointer to row i-1 in the cell array
            %kalman.current.last = ptr_imo;  % TODO do we really need this? probably not
            %kalman.current.local = ptr_imo + 1;
            kalman.current.step  = kalman.steps{ptr_imo}.step + 1;

            n_imo = kalman.steps{ptr_imo}.dimension;                             % n_{i-1}

            l_i = size(F_i,1);                                             % row dimension
            if size(H_i,1) ~= l_i                                          % this allows the user to pass [] for H
                if l_i == n_i
                    H_i = eye(l_i);
                else
                    H_i = [ eye(l_i) zeros(l_i,n_i - l_i)];
                end
            end

            V_i_F_i  = - K_i.weigh(F_i);
            V_i_c_i =    K_i.weigh(c_i);
            V_i_H_i  =   K_i.weigh(H_i);

            %ptr_imo = kalman.current.last;

            % we denote by z_i the row dimension of Rtilde_{i-1,i-1}
            if isfield(kalman.steps{ptr_imo},'Rdiag') ...
               && ~isempty(kalman.steps{ptr_imo}.Rdiag)                    % coverage tested in ... ?
                z_i = size(kalman.steps{ptr_imo}.Rdiag,1);                 
                A = [ kalman.steps{ptr_imo}.Rdiag ; V_i_F_i ];
                B = [ zeros( z_i, n_i)            ; V_i_H_i ];
                y = [ kalman.steps{ptr_imo}.y ; V_i_c_i ];
            else
                %z_i = 0;
                A = V_i_F_i;
                B = V_i_H_i;
                y = V_i_c_i;
            end

            [Q,R] = qr(A);
            B = Q' * B;
            y = Q' * y;
            kalman.steps{ptr_imo}.Rdiag    = R(1:min(size(A,1),n_imo),:);
            kalman.steps{ptr_imo}.Rsupdiag = B(1:min(size(B,1),n_imo),:);
            kalman.steps{ptr_imo}.y        = y(1:min(length(y),n_imo),1);

            % block row i-1 is now sealed

            if (size(B,1) > n_imo)                                         % we have leftover rows that go into the Rbar
                kalman.current.Rbar = B(n_imo+1:end,:);
                kalman.current.ybar = y(n_imo+1:end,1);
            end
        end

        function observe(kalman,G_i,o_i,C_i)
            %OBSERVE   Provide observations of the current state.
            %   kalman.OBSERVE(G_i,o_i,C_i) provide observations that satisfy
            %   the linear equation
            %                o_i = G_i * u_i + delta_i
            %   where C_i is the covariance matrix of the error term delta_i.
            %   The covariance matrix C_i must be an instance of
            %   CovarianceMatrix. The matrix G_i is a standard Matlab
            %   matrix, and bo is a standard column vector. 
            % 
            %   kalman.OBSERVE() tells the algorithm that no
            %   observations are availble of the state of this step.
            % 
            %   This method must be called after advance and evolve.
            n_i = kalman.current.dimension;
            if nargin<4 || isempty(o_i) % no observations, pass []
                %m_i = 0;
                if isfield(kalman.current,'Rbar') && ~isempty(kalman.current.Rbar)
                    [Q,R] = qr( kalman.current.Rbar );
                    kalman.current.Rdiag = R;
                    kalman.current.y     = Q' * kalman.current.ybar ;
                end
            else % no observations
                %m_i = length(o_i);

                W_i_G_i = C_i.weigh(G_i);
                W_i_o_i = C_i.weigh(o_i);

                if isfield(kalman.current,'Rbar') && ~isempty(kalman.current.Rbar)
                    [Q,R] = qr( [ kalman.current.Rbar ; W_i_G_i ] , 0 ); % thin QR
                    kalman.current.Rdiag = R;
                    kalman.current.y   = Q' * [ kalman.current.ybar ; W_i_o_i ];
                else % no Rdiag yet
                    RdiagRowDim = min(n_i, size(W_i_G_i,1));
                    [Q,R] = qr(W_i_G_i);
                    kalman.current.Rdiag = R(1:RdiagRowDim,1:n_i);
                    y                  = Q' * W_i_o_i;
                    kalman.current.y   = y(1:RdiagRowDim,1);
                end
            end % of we have observations

            if size(kalman.current.Rdiag,1) == kalman.current.dimension
                kalman.current.estimatedState      = kalman.current.Rdiag \ kalman.current.y;
                kalman.current.estimatedCovariance = CovarianceMatrix( kalman.current.Rdiag, 'W');
            end

            kalman.steps{ length(kalman.steps)+1 } = kalman.current;
        end

        %function [estimate,cov] = filtered(kalman)
            %FILTERED   Obtain an estimate of the current state and the 
            %           covariance of the estimate.
            %
            %   kalman.FILTERED() returns the estimate of the state of the
            %   last step that was observed (even if the observation was
            %   empty).
            %
            %   The method also returns the covariance of the estimate. The
            %   covariance is an instance of CovarianceMatrix.
            % 
            %   This method can only be called after observe.

        %    l = length(kalman.steps);
        %    estimate = kalman.steps{l}.Rdiag \ kalman.steps{l}.y;
        %    cov = kalman.steps{l}.estimatedCovariance;
        %end

        function [estimate,cov] = estimate(kalman,s)
            %ESTIMATE  Obtain the most recent estimate of the state of a step
            %          and the covariance of the estimate.
            %
            %   kalman.ESTIMATE(s) returns an estimate of the state of step
            %   s, which must still be in memory. It also returns the 
            %   covariance of the estimate.
            %
            %   If kalman.smooth() was not called after step s was
            %   observed, the estimate is a filtered estimate. Otherwise it
            %   is the most recent smoothed estimate.
            
            %length(kalman.steps)
            l = length(kalman.steps);
            latest   = kalman.steps{l}.step;
            earliest = kalman.steps{1}.step;
            if s < earliest || s > latest 
                warning('cannot provide an estimate, too old or in the future')
                return
            end
            ptr_s = s - earliest + 1;

            if isfield(kalman.steps{ptr_s},'estimatedState') && ~isempty(kalman.steps{ptr_s}.estimatedState)
                estimate = kalman.steps{ptr_s}.estimatedState;
                cov      = kalman.steps{ptr_s}.estimatedCovariance;
            else
                estimate = NaN * zeros(kalman.steps{ptr_s}.dimension,1);
                cov      = CovarianceMatrix(NaN*eye(kalman.steps{ptr_s}.dimension),'W');
                warning('state is currently underdetermined');
            end
        end

        function forget(kalman,s)
            %FORGET  Forget the oldest steps to save memory meory.
            %
            %   kalman.FORGET()  forgets all but the last step.
            %   
            %   kalman.FORGET(s) forgets steps s and older. 
            
            l = length(kalman.steps);
            earliest = kalman.steps{ 1 }.step;
            if nargin<2
                ptr_s = l-1;
            else
                ptr_s = s - earliest + 1;
            end
            kalman.steps = { kalman.steps{ptr_s+1:l} };
        end

        function rollback(kalman,s)
            %FORGET  Forget the oldest steps to save memory meory.
            %
            %   kalman.ROLLBACK(s) rolls back the filter to its state immediately
            %   after step s has been evolved (but before it was observed). 
            
            l = length(kalman.steps);
            earliest = kalman.steps{ 1 }.step;
            ptr_s = s - earliest + 1;
            if ptr_s > l || ptr_s < 1
                warning('cannot roll back to this state (too old or future');
            else
                kalman.current = [];
                kalman.current.dimension = kalman.steps{ ptr_s }.dimension;
                kalman.current.step      = kalman.steps{ ptr_s }.step;
                kalman.current.Rbar      = kalman.steps{ ptr_s }.Rbar;
                kalman.current.ybar      = kalman.steps{ ptr_s }.ybar;
                %kalman.current = kalman.steps{ ptr_s };
                kalman.steps = { kalman.steps{1:ptr_s-1} };
                %kalman.current = rmfield(kalman.current,{'Rdiag','Rsupdiag','y','estimatedState','estimatedCovariance'});
                %ptr_s
                %kalman
                %kalman.steps
                %kalman.current
            end
        end
        
        function smooth(kalman)
            %SMOOTH  Compute smooth estimates of all the stored states.
            %
            %   kalman.SMOOTH() computes the smoothed estimated state of all
            %   the steps that are still in memory.
            % 
            %   This method must be called after the last step has been
            %   observed.

            l = length(kalman.steps);

            v = [];
            for i=l:-1:1
                if i == l
                    v = kalman.steps{i}.y;
                else
                    v = kalman.steps{i}.y - (kalman.steps{i}.Rsupdiag) * v;
                end

                v = (kalman.steps{i}.Rdiag) \ v;

                kalman.steps{i}.estimatedState = v;
            end

            R = [];
            for i=l:-1:1
                if i == l
                    R = kalman.steps{i}.Rdiag;
                    % the covariance matrix has already been constructed
                    % here.
                else
                    n_i   = size(R,1);
                    n_imo = size(kalman.steps{i}.Rdiag,1);

                    [Q,~] = qr( [ kalman.steps{i}.Rsupdiag ; R ]);

                    S = Q' * [ kalman.steps{i}.Rdiag ; zeros(n_i,size(kalman.steps{i}.Rdiag,2)) ];
                    R = S( n_i+1:n_i+n_imo , 1:n_imo );

                    kalman.steps{i}.estimatedCovariance = CovarianceMatrix( R, 'W' );
                end
            end
        end

        %============== OLD STUFF BELOW ==============

        function [A,b] = rawLS(obj)
            if isfield(obj.steps{1},'WG')
                A = obj.steps{1}.WG;
                b = obj.steps{1}.Wbo;
            else
                A = [];
                b = [];
            end
            for i=2:obj.k
                [m,n] = size(A);
                d = size(obj.steps{i}.WF,1);
                A = [ A                            zeros(m,d)
                    zeros(d,n-d) -obj.steps{i}.WF obj.steps{i}.WI ];
                b = [ b
                    obj.steps{i}.Wbe ];
                if isfield(obj.steps{i},'WG')
                    l = size(obj.steps{i}.WG,1);
                    A = [ A
                        zeros(l,n) obj.steps{i}.WG ];
                    b = [ b
                        obj.steps{i}.Wbo ];
                end
            end
        end
        function [R,y] = triangularLS(obj)
            R   = obj.steps{1}.Rdiag;
            y = obj.steps{1}.y;
            %size(R)
            for i=2:obj.k-1
                %i
                [m,n] = size(R);
                d = size(obj.steps{i}.Rdiag,1);
                %[m n d]
                R = [ R         [ zeros(m-d,d) ; obj.steps{i-1}.Rsupdiag ]
                    zeros(d,n) obj.steps{i}.Rdiag ];
                y = [ y
                    obj.steps{i}.y ];
            end
            i = obj.k;
            [m,n] = size(R);
            %disp('zzz');
            %i
            %size(obj.steps{i-1}.Rsupdiag,1)
            l=size(obj.steps{i}.Rdiag,1);
            %[m n d]
            %size(R)
            R = [ R         [ zeros(m-d,d) ; obj.steps{i-1}.Rsupdiag ]
                zeros(l,n) obj.steps{i}.Rdiag ];
            y = [ y
                obj.steps{i}.y ];
        end
    end
end